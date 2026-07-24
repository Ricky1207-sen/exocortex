#ifndef Config_h
#define Config_h

// ============================================================
// MONEO V2 — Config.h
// ============================================================

// ─── PIN CONFIGURATION ───────────────────────────────────────
const int TOUCH_PIN       = 1;
const int SD_CARD_PIN     = 21;
const int I2S_BCLK_PIN    = 42;
const int I2S_LRCLK_PIN   = 41;

// ─── STATUS LED ──────────────────────────────────────────────
// The XIAO ESP32-S3 user LED is active-LOW: driving the pin LOW lights it,
// HIGH turns it off. Use these names so the polarity is never guessed again.
#define LED_ON   LOW
#define LED_OFF  HIGH

// ─── TOUCH SENSOR ────────────────────────────────────────────
const int          TOUCH_THRESHOLD  = 50000;
const unsigned long DEBOUNCE_DELAY  = 1000;

// ─── STARTUP ─────────────────────────────────────────────────
const unsigned long SERIAL_WAIT_MS = 100;   // max wait for USB serial at boot

// ─── AUDIO SETTINGS ──────────────────────────────────────────
const int SAMPLE_RATE     = 16000;
const int BITS_PER_SAMPLE = 8;        // 8-bit audio
const int NUM_CHANNELS    = 1;
const int I2S_BUFFER_SIZE = 512;

// ─── RECORDING SETTINGS ──────────────────────────────────────
// Audio is captured into PSRAM buffers, then appended to the single WAV file.
// The buffer size sets how much audio each save holds (a full buffer = one
// save), so it also determines the segment length.
const size_t PSRAM_BUFFER_SIZE = 160000;           // 10s * 16000Hz * 1 byte = 160KB

// ─── TIME / NTP ──────────────────────────────────────────────
// Clock is synced once at boot (after WiFi). The ESP32's internal RTC keeps
// time on its own afterwards, so no repeat NTP requests are needed.
#define NTP_SERVER  "pool.ntp.org"
const long GMT_OFFSET_SEC      = 19800;   // UTC+5:30 (IST)
const int  DAYLIGHT_OFFSET_SEC = 0;

// ─── WIFI — add as many networks as needed ───────────────────
// Format: { "SSID", "PASSWORD" }
// Device tries each in order, connects to first available
#define WIFI_NETWORK_COUNT 2
extern const char* WIFI_NETWORKS[WIFI_NETWORK_COUNT][2];
const unsigned long WIFI_CONNECT_TIMEOUT_MS  = 10000;
const unsigned long WIFI_RECONNECT_INTERVAL  = 5000;

// ─── AI API — auto-detected from key prefix ──────────────────
// Paste your own API key here. Provider is detected automatically:
//   sk-...        → OpenAI  (gpt-4o-audio-preview)
//   AIza...       → Gemini  (gemini-2.5-flash-lite)
//   gsk_...       → Groq
#define AI_API_KEY   "YOUR_API_KEY_HERE"

// ─── FEATURE TOGGLES ─────────────────────────────────────────
#define DEBUG_LOGS_ENABLED    1

// ─── DEBUG HELPER ────────────────────────────────────────────
#if DEBUG_LOGS_ENABLED
  #define DLOG(x)     Serial.println(x)
  #define DLOGF(...)  Serial.printf(__VA_ARGS__)
#else
  #define DLOG(x)
  #define DLOGF(...)
#endif

#endif
