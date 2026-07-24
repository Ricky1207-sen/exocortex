#ifndef Recorder_h
#define Recorder_h

#include <Arduino.h>
#include <ESP_I2S.h>
#include <SD.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <time.h>
#include "Config.h"

// ============================================================
// Recorder — Records audio into a SINGLE WAV file.
//
// Capture never stops, even while saving (ping-pong / double buffer):
//   - Two PSRAM buffers. Capture fills one; when it's full it INSTANTLY
//     switches to the other and hands the full one to the writer.
//   - The writer saves that buffer to SD while capture keeps filling the
//     other. They never touch the same buffer, so there is NO lock.
//   - Capture sleeps inside i2s.readBytes() until the mic delivers a chunk
//     (no polling, no busy-spin).
//
// One file per session. The file is opened/appended/closed per buffer, so a
// power loss can corrupt at most the latest buffer, not the whole recording.
// ============================================================

class Recorder {
public:
    Recorder();
    bool begin();
    void startRecording(time_t now = 0);
    void stopRecording();

    bool isRecording() const { return _recording; }
    bool hasError() const { return _writeError; }
    String lastRecordingPath() const { return _wavPath; }

private:
    void _writeWavHeader(File& f, uint32_t dataLen);
    void _finalizeHeader();
    bool _writeBufferToSD(uint8_t* data, size_t len);
    String _generateFilename(time_t now);

    void _captureTask();
    void _writerTask();

    I2SClass _i2s;

    // Two ping-pong PSRAM buffers. Capture fills _buf[_activeBuf]; the writer
    // saves whichever full buffer index arrives on _flushQueue. Never touched
    // at the same time → no mutex needed.
    uint8_t* _buf[2];
    size_t   _bufFill[2];
    int      _activeBuf;

    String   _wavPath;
    uint32_t _dataLength;

    volatile bool _recording;
    volatile bool _writeError;

    QueueHandle_t     _flushQueue;   // capture → writer: "buffer N is ready to save"
    SemaphoreHandle_t _writerDone;   // writer → stop path: "I've drained and exited"

    TaskHandle_t _captureTask_h;
    TaskHandle_t _writerTask_h;
};

#endif
