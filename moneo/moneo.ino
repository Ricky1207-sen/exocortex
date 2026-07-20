// ============================================================
//  MONEO
//  moneo.ino — Main Sketch
//
//  Board:  Seeed Studio XIAO ESP32S3 Sense
//  PSRAM:  Tools → PSRAM → OPI PSRAM  ← MUST ENABLE
//
//  Flow:
//    Touch D1 → start recording (LED ON)
//    Speak for as long as needed (10s segments flushed to SD)
//    Touch D1 → stop recording (LED OFF)
//    Device connects to WiFi (WiFiMulti picks the strongest known network)
//    WAV file is saved on the SD card
//
//  NOTE: AI note-generation (AIClient) is not part of this build yet. Those
//  lines are commented out and marked TODO; they will be wired in once
//  AIClient is added to the repo.
// ============================================================

#include "Config.h"
#include "Recorder.h"
#include "TouchButton.h"
#include "WiFiManager.h"
#include <time.h>
// #include "AIClient.h"   // TODO: enable when AIClient is added

Recorder    recorder;
TouchButton button;
WiFiManager wifiMgr;
// AIClient aiClient;       // TODO: enable when AIClient is added

// The device is always in exactly one of these states.
enum AppState { STATE_IDLE, STATE_RECORDING, STATE_PROCESSING };
AppState _state = STATE_IDLE;

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

    // TODO: detect AI provider once AIClient is integrated
    // if (!aiClient.begin()) {
    //     Serial.println("[FATAL] AI client init failed. Check API key in Config.h");
    //     _errorBlink();
    // }

    // Init recorder
    if (!recorder.begin()) {
        Serial.println("[FATAL] Recorder init failed.");
        _errorBlink();
    }

    button.begin(TOUCH_PIN, TOUCH_THRESHOLD, DEBOUNCE_DELAY);

    // Sync the clock once, now, while we can reach the network. After this the
    // ESP32's internal RTC keeps time on its own — no repeat NTP requests.
    _syncTime();

    Serial.println("[Moneo] Ready. Touch pin to start.");   // now truly ready
}

// Connect WiFi once and sync the clock. If WiFi isn't available, recordings
// just fall back to uptime-based filenames — not fatal.
void _syncTime() {
    Serial.println("[Time] Connecting WiFi to sync clock...");
    if (!wifiMgr.connect()) {
        Serial.println("[Time] WiFi unavailable — using uptime-based names.");
        return;
    }
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    struct tm t;
    if (getLocalTime(&t, 5000)) {
        _syncedEpoch    = time(nullptr);   // remember the real time...
        _lastSyncMillis = millis();        // ...and when we got it
        _timeSynced     = true;
        Serial.println("[Time] Clock synced.");
    } else {
        Serial.println("[Time] NTP sync failed — using uptime-based names.");
    }
}

// Current epoch, computed from the single sync plus elapsed time — no network
// request. Returns 0 if the clock was never synced (caller falls back to an
// uptime-based name).
time_t currentEpoch() {
    if (!_timeSynced) return 0;
    return _syncedEpoch + (time_t)((millis() - _lastSyncMillis) / 1000);
}

void loop() {
    // _state is the single source of truth for the lifecycle. A touch drives
    // the transitions; we don't mirror the recorder's internal flag here.
    switch (_state) {
        case STATE_IDLE:
            // Idle until a touch starts a recording.
            if (button.pressed()) {
                recorder.startRecording();
                _state = STATE_RECORDING;
            }
            break;

        case STATE_RECORDING:
            // Recording until the next touch stops it.
            if (button.pressed()) {
                recorder.stopRecording();
                _state = STATE_PROCESSING;
            }
            break;

        case STATE_PROCESSING:
            // Do the (slow) WiFi work once, then return to idle.
            _handleRecording();
            _state = STATE_IDLE;
            break;
    }

    delay(100);
}

// Runs once per recording, in the PROCESSING state.
void _handleRecording() {
    if (recorder.hasError()) {
        // Write failed mid-way; the partial file was finalized. Signal it.
        Serial.println("[Moneo] Recording FAILED (SD write error). File may be incomplete.");
        _errorBlink();   // halts here, blinking the LED
        return;
    }

    String wavPath = recorder.lastRecordingPath();
    if (wavPath.length() > 0) {
        _processRecording(wavPath);
    }
}

void _processRecording(const String& wavPath) {
    Serial.println("[Moneo] Recording complete. Connecting to WiFi...");

    // Blink LED while connecting
    digitalWrite(LED_BUILTIN, LED_ON);
    delay(200);
    digitalWrite(LED_BUILTIN, LED_OFF);

    if (!wifiMgr.connect()) {
        Serial.println("[Moneo] WiFi failed.");
        Serial.println("[Moneo] WAV file saved locally: " + wavPath);
        // Graceful failure — the WAV is safe on the SD card
        return;
    }

    Serial.println("[Moneo] WiFi connected.");
    Serial.printf("[Moneo] File saved: %s\n", wavPath.c_str());

    // TODO: AIClient integration — send the WAV to the AI and save notes.
    // Enabled once AIClient is added to the repo.
    //
    // String notes = aiClient.generateNotes(wavPath);
    // if (notes.isEmpty()) {
    //     Serial.println("[Moneo] AI returned no notes.");
    //     return;
    // }
    // String notesPath = wavPath;
    // notesPath.replace(".wav", ".md");          // e.g. rec_….wav → rec_….md
    // File f = SD.open(notesPath.c_str(), FILE_WRITE);
    // if (f) {
    //     f.print(notes);
    //     f.close();
    //     Serial.println("[Moneo] ✓ Notes saved: " + notesPath);
    // } else {
    //     Serial.println("[Moneo] Failed to save notes to SD.");
    // }

    Serial.println("[Moneo] Done! Touch pin to record again.");
}

void _errorBlink() {
    while (true) {
        digitalWrite(LED_BUILTIN, LED_ON);  delay(200);
        digitalWrite(LED_BUILTIN, LED_OFF); delay(200);
    }
}
