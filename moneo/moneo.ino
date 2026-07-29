// ============================================================
//  MONEO
//  moneo.ino — Main Sketch
//
//  Board:  Seeed Studio XIAO ESP32S3 Sense
//  PSRAM:  Tools → PSRAM → OPI PSRAM  ← MUST ENABLE
//
//  Flow:
//    Boot: connect WiFi → sync clock → disconnect WiFi
//    Touch D1 → start recording (LED ON)
//    Speak; 10-second ping-pong segments flush to SD without pausing capture
//    Touch D1 → stop recording → device immediately ready for next recording
//    Background: reconnect WiFi → upload / transcribe → disconnect WiFi
//
//  AIClient sends each completed WAV file to the configured provider and saves
//  the generated notes beside it as a Markdown file.
// ============================================================

#include "Config.h"
#include "Recorder.h"
#include "TouchButton.h"
#include "WiFiManager.h"
#include "AIClient.h"
#include <time.h>

Recorder    recorder;
TouchButton button;
WiFiManager wifiMgr;
AIClient    aiClient;

// Queue of WAV paths pending upload/transcription (see _processorLoop).
QueueHandle_t _procQueue;

// Manual clock: captured once at NTP sync, then the current time is computed as
// (synced time + elapsed millis) — no further network requests are ever made.
time_t        _syncedEpoch    = 0;   // real epoch captured at the moment of sync
unsigned long _lastSyncMillis = 0;   // millis() at that same moment
bool          _timeSynced     = false;

void setup() {
    Serial.begin(115200);
    unsigned long serialTimeout = millis() + SERIAL_WAIT_MS;
    while (!Serial && millis() < serialTimeout) { delay(10); }

    Serial.println("╔══════════════════════════════════╗");
    Serial.println("║         MONEO — Starting         ║");
    Serial.println("╚══════════════════════════════════╝");

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);

    if (!aiClient.begin()) {
        Serial.println("[FATAL] AI client init failed. Check API key in Config.h");
        _errorBlink();
    }

    if (!recorder.begin()) {
        Serial.println("[FATAL] Recorder init failed.");
        _errorBlink();
    }

    button.begin(TOUCH_PIN, TOUCH_THRESHOLD, DEBOUNCE_DELAY);

    _syncTime();   // connect WiFi, sync RTC, disconnect

    // Processor task (core 0) handles post-recording upload in the background.
    _procQueue = xQueueCreate(4, sizeof(String*));
    xTaskCreatePinnedToCore([](void*){ _processorLoop(); },
                            "Processor", 8192, nullptr, 2, nullptr, 0);
    Serial.printf("[Moneo] AI Provider: %s\n", aiClient.providerName());
}

void loop() {
    if (button.pressed()) {
        if (!recorder.isRecording()) {
            recorder.startRecording(currentEpoch());
        } else {
            recorder.stopRecording();
            _enqueueProcessing(recorder.lastRecordingPath());
        }
    }
    delay(100);
}

// Syncs RTC once at boot; WiFi is disconnected immediately after (stays off
// during recording to avoid RF interference). Falls back to uptime filenames.
void _syncTime() {
    DLOG("[Time] Syncing clock...");
    if (!wifiMgr.connect()) {
        DLOG("[Time] WiFi unavailable — filenames will use uptime.");
        return;
    }
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    struct tm t;
    if (getLocalTime(&t, 5000)) {
        _syncedEpoch    = time(nullptr);
        _lastSyncMillis = millis();
        _timeSynced     = true;
        DLOG("[Time] Clock synced.");
    } else {
        DLOG("[Time] NTP failed — filenames will use uptime.");
    }
    wifiMgr.disconnect();
}

// Epoch from boot-time NTP sync + elapsed millis. Returns 0 if never synced.
time_t currentEpoch() {
    if (!_timeSynced) return 0;
    return _syncedEpoch + (time_t)((millis() - _lastSyncMillis) / 1000);
}

void _enqueueProcessing(const String& wavPath) {
    if (recorder.hasError()) {
        DLOG("[Moneo] Write errors — file may be incomplete.");
        _errorSignal();
        return;
    }
    if (wavPath.isEmpty()) return;

    String* p = new String(wavPath);
    if (xQueueSend(_procQueue, &p, 0) != pdTRUE) {
        DLOGF("[Moneo] Queue full — skipping %s.\n", wavPath.c_str());
        delete p;
    }
}

// Background task (core 0). Processes one recording at a time; blocks when the
// queue is empty. A new recording can start on core 1 while this runs.
void _processorLoop() {
    while (true) {
        String* p;
        xQueueReceive(_procQueue, &p, portMAX_DELAY);
        _processRecording(*p);
        delete p;
    }
}

void _processRecording(const String& wavPath) {
    Serial.println("[Moneo] Connecting to WiFi...");
    if (!wifiMgr.connect()) {
        DLOGF("[Moneo] WiFi unavailable — %s saved locally.\n", wavPath.c_str());
        return;
    }

    Serial.printf("[Moneo] WiFi connected. File: %s\n", wavPath.c_str());

    String notes = aiClient.generateNotes(wavPath);
    if (notes.isEmpty()) {
        Serial.println("[Moneo] AI returned no notes.");
        wifiMgr.disconnect();
        return;
    }

    String notesPath = wavPath;
    notesPath.replace(".wav", ".md");
    File f = SD.open(notesPath.c_str(), FILE_WRITE);
    if (f) {
        f.print(notes);
        f.close();
        Serial.println("[Moneo] Notes: " + notesPath);
    } else {
        Serial.println("[Moneo] Failed to save notes.");
    }

    wifiMgr.disconnect();
    DLOGF("[Moneo] Processed: %s\n", wavPath.c_str());
}

// Hard halt for unrecoverable init failures (hardware is broken).
void _errorBlink() {
    while (true) {
        digitalWrite(LED_BUILTIN, LED_ON);  delay(200);
        digitalWrite(LED_BUILTIN, LED_OFF); delay(200);
    }
}

// Three quick flashes for a recoverable error; returns when done.
void _errorSignal() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, LED_ON);  delay(100);
        digitalWrite(LED_BUILTIN, LED_OFF); delay(100);
    }
}
