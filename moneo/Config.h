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
const int BITS_PER_SAMPLE = 8;
const int NUM_CHANNELS    = 1;
const int I2S_BUFFER_SIZE = 512;

// ─── RECORDING SETTINGS ──────────────────────────────────────
// Each PSRAM buffer holds exactly SEGMENT_DURATION_S of audio. When full, it
// is handed to the writer while capture continues into the other (ping-pong).
const unsigned int SEGMENT_DURATION_S = 10;
const size_t       PSRAM_BUFFER_SIZE  = (size_t)SAMPLE_RATE
                                       * NUM_CHANNELS
                                       * (BITS_PER_SAMPLE / 8)
                                       * SEGMENT_DURATION_S;

// ─── TIME / NTP ──────────────────────────────────────────────
// Clock is synced once at boot (after WiFi), then WiFi is disconnected.
// The ESP32's RTC tracks time from that point; no repeat NTP requests needed.
#define NTP_SERVER  "pool.ntp.org"
const long GMT_OFFSET_SEC      = 0;       // UTC; adjust for local timezone
const int  DAYLIGHT_OFFSET_SEC = 0;

// ─── WIFI — add as many networks as needed ───────────────────
// Format: { "SSID", "PASSWORD" }
#define WIFI_NETWORK_COUNT 2
extern const char* WIFI_NETWORKS[WIFI_NETWORK_COUNT][2];
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;

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
