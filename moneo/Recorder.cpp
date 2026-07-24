#include "Recorder.h"
#include <SD.h>
#include <FS.h>
#include <time.h>

// Sentinel index posted to _flushQueue to tell the writer "no more buffers,
// finish and exit." Real buffer indexes are only ever 0 or 1.
static const int FLUSH_STOP = -1;

Recorder::Recorder()
    : _activeBuf(0), _dataLength(0),
      _recording(false), _writeError(false),
      _flushQueue(nullptr), _writerDone(nullptr),
      _captureTask_h(nullptr), _writerTask_h(nullptr)
{
    _buf[0] = nullptr; _buf[1] = nullptr;
    _bufFill[0] = 0;   _bufFill[1] = 0;
}

bool Recorder::begin() {
    if (_buf[0]) return true;   // already initialized
    if (!SD.begin(SD_CARD_PIN)) {
        DLOG("[Recorder] SD card mount failed!");
        return false;
    }
    DLOG("[Recorder] SD card mounted.");

    // Two ping-pong buffers in PSRAM (2 × PSRAM_BUFFER_SIZE; trivial for the 8MB PSRAM).
    _buf[0] = (uint8_t*)ps_malloc(PSRAM_BUFFER_SIZE);
    _buf[1] = (uint8_t*)ps_malloc(PSRAM_BUFFER_SIZE);
    if (!_buf[0] || !_buf[1]) {
        DLOG("[Recorder] PSRAM allocation failed!");
        return false;
    }
    DLOG("[Recorder] PSRAM buffers allocated (x2).");

    _flushQueue = xQueueCreate(4, sizeof(int));
    _writerDone = xSemaphoreCreateBinary();
    if (!_flushQueue || !_writerDone) {
        DLOG("[Recorder] Queue/semaphore creation failed!");
        return false;
    }

    _i2s.setPinsPdmRx(I2S_BCLK_PIN, I2S_LRCLK_PIN);
    if (!_i2s.begin(I2S_MODE_PDM_RX, SAMPLE_RATE,
                    I2S_DATA_BIT_WIDTH_8BIT, I2S_SLOT_MODE_MONO)) {
        DLOG("[Recorder] I2S init failed!");
        return false;
    }
    DLOGF("[Recorder] I2S initialized (%d Hz, %d-bit).\n",
          SAMPLE_RATE, BITS_PER_SAMPLE);

    DLOG("[Recorder] Ready. Touch pin to start.");
    return true;
}

// ── Generate datetime filename ─────────────────────────────
String Recorder::_generateFilename(time_t now) {
    if (now > 0) {
        struct tm* lt = localtime(&now);
        char buf[32];
        strftime(buf, sizeof(buf), "/rec_%Y%m%d_%H%M%S.wav", lt);
        return String(buf);
    }

    // Fallback: clock never synced — use an uptime-based name.
    unsigned long secs = millis() / 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "/rec_%05lu.wav", secs);
    return String(buf);
}

// ── WAV header ─────────────────────────────────────────────
void Recorder::_writeWavHeader(File& f, uint32_t dataLen) {
    uint32_t sampleRate  = SAMPLE_RATE;
    uint16_t bitsPerSamp = BITS_PER_SAMPLE;
    uint16_t numChan     = NUM_CHANNELS;
    uint32_t byteRate    = sampleRate * numChan * bitsPerSamp / 8;
    uint16_t blockAlign  = numChan * bitsPerSamp / 8;
    uint32_t chunkSize   = dataLen + 36;
    uint32_t subChunk1   = 16;
    uint16_t audioFormat = 1;

    f.seek(0);
    f.write((const uint8_t*)"RIFF", 4);
    f.write((uint8_t*)&chunkSize,   4);
    f.write((const uint8_t*)"WAVE", 4);
    f.write((const uint8_t*)"fmt ", 4);
    f.write((uint8_t*)&subChunk1,   4);
    f.write((uint8_t*)&audioFormat, 2);
    f.write((uint8_t*)&numChan,     2);
    f.write((uint8_t*)&sampleRate,  4);
    f.write((uint8_t*)&byteRate,    4);
    f.write((uint8_t*)&blockAlign,  2);
    f.write((uint8_t*)&bitsPerSamp, 2);
    f.write((const uint8_t*)"data", 4);
    f.write((uint8_t*)&dataLen,     4);
}

// ── Append one buffer to the WAV file ──────────────────────────
bool Recorder::_writeBufferToSD(uint8_t* data, size_t len) {
    if (len == 0) return true;

    File f = SD.open(_wavPath.c_str(), FILE_APPEND);
    if (!f) {
        DLOG("[Writer] ERROR: cannot open WAV to append.");
        return false;
    }

    size_t written = f.write(data, len);
    f.close();

    _dataLength += written;

    if (written != len) {
        DLOGF("[Writer] ERROR: short write (%u of %u bytes).\n", written, len);
        return false;
    }

    DLOGF("[Writer] Saved %u bytes (total: %u)\n", written, _dataLength);
    return true;
}

// ── Start ──────────────────────────────────────────────────
void Recorder::startRecording(time_t now) {
    DLOG("[Recorder] Starting...");
    _writeError  = false;
    _dataLength  = 0;
    _activeBuf   = 0;
    _bufFill[0]  = 0;
    _bufFill[1]  = 0;

    xQueueReset(_flushQueue);   // discard stale items from any previous run
    _wavPath = _generateFilename(now);
    DLOGF("[Recorder] File: %s\n", _wavPath.c_str());

    // Create the file and lay down a placeholder header, then close it.
    // Buffers are appended afterwards; the real length is written on stop.
    File f = SD.open(_wavPath.c_str(), FILE_WRITE);
    if (!f) {
        DLOG("[Recorder] Cannot create WAV file!");
        return;
    }
    _writeWavHeader(f, 0);
    f.close();

    _recording = true;
    digitalWrite(LED_BUILTIN, LED_ON);

    // Capture: I2S reads + pointer arithmetic + one queue send per segment.
    xTaskCreatePinnedToCore([](void* arg){ ((Recorder*)arg)->_captureTask(); },
                            "Capture", 4096, this, 5, &_captureTask_h, 1);
    // Writer: SD open/write/close per segment; SD library needs more stack.
    xTaskCreatePinnedToCore([](void* arg){ ((Recorder*)arg)->_writerTask(); },
                            "Writer",  8192, this, 3, &_writerTask_h,  1);

    DLOG("[Recorder] Recording started.");
}

// ── Stop ───────────────────────────────────────────────────
void Recorder::stopRecording() {
    DLOG("[Recorder] Stopping...");
    _recording = false;      // capture finishes its current chunk, then exits
    digitalWrite(LED_BUILTIN, LED_OFF);

    // Wait for the writer to drain the queue and signal completion.
    if (xSemaphoreTake(_writerDone, pdMS_TO_TICKS(10000)) != pdTRUE) {
        DLOG("[Recorder] ERROR: writer timed out — force-stopping tasks.");
        if (_captureTask_h) { vTaskDelete(_captureTask_h); _captureTask_h = nullptr; }
        if (_writerTask_h)  { vTaskDelete(_writerTask_h);  _writerTask_h  = nullptr; }
        _writeError = true;
    }

    _finalizeHeader();

    DLOG("[Recorder] Stopped.");
}

// ── Finalize the WAV header ─────────────────────────────────
// Reopen the file to write the real data length into the header. r+ keeps the
// existing audio; it only overwrites the 44-byte header at the start.
void Recorder::_finalizeHeader() {
    File f = SD.open(_wavPath.c_str(), "r+");
    if (f) {
        _writeWavHeader(f, _dataLength);
        f.close();
        DLOGF("[Recorder] WAV saved: %s (%u bytes)\n",
              _wavPath.c_str(), _dataLength);
    } else {
        DLOG("[Recorder] Could not reopen WAV to finalize header!");
    }
}

// ── Capture task ───────────────────────────────────────────
void Recorder::_captureTask() {
    DLOG("[Capture] Task started.");

    while (_recording) {
        size_t space  = PSRAM_BUFFER_SIZE - _bufFill[_activeBuf];
        size_t toRead = min((size_t)I2S_BUFFER_SIZE, space);

        // Sleeps inside the I2S driver until the mic delivers a chunk.
        int n = _i2s.readBytes((char*)(_buf[_activeBuf] + _bufFill[_activeBuf]), toRead);
        if (n > 0) _bufFill[_activeBuf] += n;

        // Buffer full → hand to writer and flip instantly; capture never pauses.
        if (_bufFill[_activeBuf] >= PSRAM_BUFFER_SIZE) {
            int full = _activeBuf;
            _activeBuf = 1 - _activeBuf;   // 0<->1
            _bufFill[_activeBuf] = 0;      // fresh buffer starts empty
            xQueueSend(_flushQueue, &full, portMAX_DELAY);
        }
    }

    // Hand over the partial tail buffer, then signal the writer to exit.
    if (_bufFill[_activeBuf] > 0) {
        int last = _activeBuf;
        xQueueSend(_flushQueue, &last, portMAX_DELAY);
    }
    int stop = FLUSH_STOP;
    xQueueSend(_flushQueue, &stop, portMAX_DELAY);

    DLOG("[Capture] Task finished.");
    _captureTask_h = nullptr;
    vTaskDelete(nullptr);
}

// ── Writer task ────────────────────────────────────────────
void Recorder::_writerTask() {
    DLOG("[Writer] Task started.");

    while (true) {
        int idx;
        // Sleeps until capture posts a full buffer (or FLUSH_STOP). No polling.
        xQueueReceive(_flushQueue, &idx, portMAX_DELAY);
        if (idx == FLUSH_STOP) break;

        // Capture owns the OTHER buffer; this one is ours alone — no lock.
        // On write failure, keep draining so capture never blocks.
        if (!_writeBufferToSD(_buf[idx], _bufFill[idx])) {
            _writeError = true;
        }
    }

    DLOG("[Writer] Task finished.");
    xSemaphoreGive(_writerDone);   // let stopRecording() proceed
    _writerTask_h = nullptr;
    vTaskDelete(nullptr);
}
