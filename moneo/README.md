# 👁️‍🗨️ Moneo

> द्वा सुपर्णा सयुजा सखाया समानं वृक्षं परिषस्वजाते।  
> तयोरन्यः पिप्पलं स्वाद्वत्त्यनश्नन्नन्यो अभिचाकशीति ॥

Second-stage runtime for Manas: **Moneo**&mdash;*The Meek Mnemonic Majordomo!*

A smart wearable audio recorder that captures a continuous, unbroken session to a single WAV file using the device's PSRAM as a ping-pong double buffer, then transcribes the recording via a remote LLM API and saves the transcript alongside the audio. Both files can optionally be uploaded to a remote storage server for downstream retrieval and agentic integration.

## ✨ Features

- **Continuous Recording**: Two PSRAM ping-pong buffers (2 × 10 s of 16 kHz 8-bit mono PCM) flush incrementally to a single WAV file — capture never pauses while the other buffer saves to SD. Power-loss safe: at most the last 10-second segment is at risk.
- **Datetime Filenames**: NTP time is fetched at boot (WiFi disconnected immediately after); recordings are named `rec_YYYYMMDD_HHMMSS.wav` in the SD root. Falls back to `rec_NNNNN.wav` (uptime seconds) if NTP is unavailable.
- **LLM Transcription**: After a session ends, the WAV file is POST-ed to a configurable OpenAI-compatible Whisper endpoint. The returned transcript is written as a Markdown file at the same path (`.wav` → `.md`).
- **NVS Configuration**: All runtime parameters (WiFi credentials, API endpoints, pin assignments) live in ESP32 Non-Volatile Storage via the `Preferences` library, organised by namespace. Seeding NVS is as simple as dropping a `config.json` on the SD card — updating existing config is just the same.
- **Multi-WiFi Support**: Multiple WiFi networks provisioned in `Config.h` (home, office, hotspot, etc.); WiFiMulti connects to the strongest available signal. NVS-backed credentials are on the roadmap.
- **Non-blocking Processing**: After a session ends, transcription and upload run in a background FreeRTOS task pinned to core 0 — the device is immediately ready for the next recording.
- **RF-safe Recording**: WiFi stays off during capture to avoid I2S/radio interference; it is reconnected on demand only for post-processing.
- **Optional Cloud Sync**: Both the WAV and Markdown files can be uploaded to a remote file server after transcription.
- **Touch Control**: Capacitive touch start/stop, same as Marci.
- **Visual Feedback**: LED on during recording, off at idle.

## 🛠️ Hardware

Same base as Marci — PSRAM is now actively required:

- **Board**: Seeed Studio XIAO ESP32S3 Sense
- **Microphone**: Onboard PDM microphone
- **Storage**: MicroSD card (FAT32 formatted)
- **PSRAM**: 8 MB OPI PSRAM (**required** — used for the audio ring buffer)
- **Touch Sensor**: Capacitive surface on configurable GPIO pin
- **WiFi**: 802.11 b/g/n for NTP sync, LLM transcription, and optional upload

### Data Flow

```
[PDM Mic] → I2S → [PSRAM ping-pong buffer] → (flush) → [SD: session.wav]
                                                          ↓  (on session end)
                                               [LLM API] → [SD: session.md]
                                                          ↓  (optional)
                                                   [Remote file server]
```

## 🚀 Setup

### 1. Arduino IDE Configuration

- Board: `Seeed Studio XIAO ESP32S3`
- PSRAM: `Tools → PSRAM → OPI PSRAM` (**required**)
- Libraries to install via Library Manager:
  - `ArduinoJson` — needed once config loading and LLM response parsing are enabled
- Built-in (no install needed, currently used): `WiFi`, `SD`, `ESP_I2S`, `time.h`
- Built-in (no install needed, required once upload/transcription is enabled): `HTTPClient`, `WiFiClientSecure`, `Preferences`
- Serial Monitor: 115200 baud

### 2. NVS Configuration via `config.json`

On first flash the device has no NVS entries and falls back to compile-time defaults in `Config.h`. To provision runtime config — or to update it later — place a `config.json` file in the **root of the MicroSD card** before booting. During startup, if the file is detected, every value in it is written into the corresponding NVS namespace and key; the file is then renamed to `config.bak` to avoid repeating the process during the next boot.

#### `config.json` schema

Top-level keys are **NVS namespace names**. Each namespace holds its own structure:

> [!NOTE]
> The following schema is for indicative purposes only. The exact structure is subject to change, due to implementation limitations/conveniences. In such scenario, please make sure to document the change.

```json
{
  "wifi": {
    "HomeNetwork": "home-password",
    "OfficeWiFi": "office-password",
    "PhoneHotspot": "hotspot-password"
  },
  "llm": {
    "host": "api.openai.com",
    "port": 443,
    "path": "/v1/audio/transcriptions",
    "key": "sk-...",
    "model": "whisper-1"
  },
  "upload": {
    "host": "files.example.com",
    "port": 443,
    "path": "/recordings"
  },
  "device": {
    "touch_pin": 1,
    "touch_thresh": 50000,
    "auto_transcribe": true,
    "auto_upload": false
  }
}
```

### 3. Flash and Run

1. Compile and upload `moneo.ino`
2. On boot: device initializes SD, allocates PSRAM buffers, initializes I2S, then briefly connects WiFi to sync the clock via NTP and immediately disconnects
3. Touch the pin to start recording (LED turns on)
4. Talk — audio streams continuously to a single WAV file on the SD card
5. Touch again to stop (LED turns off); transcription and optional upload begin

## 📁 SD Card Structure

Recordings are written directly to the SD card root:

```
/
  ├── config.json                   ← optional; consumed on boot, renamed to config.bak
  ├── rec_20260528_091523.wav       ← continuous audio for the session
  ├── rec_20260528_091523.md        ← LLM transcript (written after session ends)
  ├── rec_20260528_143210.wav
  └── rec_20260528_143210.md
```

## 🌐 LLM API

Moneo targets OpenAI-compatible Whisper transcription endpoints:

- **Method**: `POST`
- **Path**: `/v1/audio/transcriptions` (configurable via NVS)
- **Body**: `multipart/form-data` with fields `file` (WAV binary) and `model`
- **Response**: `{ "text": "transcript here" }`

Compatible self-hosted backends:
- `whisper.cpp` with HTTP server mode
- `faster-whisper` + HTTP wrapper
- `LocalAI`, `Ollama` (Whisper-compatible endpoints)

For HTTPS endpoints (e.g., `api.openai.com`), `WiFiClientSecure` is required.

## 🔧 NVS Configuration Reference

Each top-level key in `config.json` maps directly to an NVS namespace. The namespaces and their keys are:

### `wifi` namespace

| `config.json` structure | NVS representation | Description |
|-------------------------|--------------------|-------------|
| `{ "<SSID>": "<pass>", …}` | indexed entries | Provisioned SSID / password pairs |

### `llm` namespace

| Key | Type | Default (Config.h) | Description |
|-----|------|--------------------|-------------|
| `host` | String | `DEFAULT_LLM_HOST` | LLM API hostname |
| `port` | Int | `DEFAULT_LLM_PORT` | LLM API port (443 for HTTPS) |
| `path` | String | `DEFAULT_LLM_PATH` | API endpoint path |
| `key` | String | — | Bearer token / API key (**never** put in Config.h) |
| `model` | String | `DEFAULT_LLM_MODEL` | Model name (e.g. `whisper-1`) |

### `upload` namespace

| Key | Type | Default (Config.h) | Description |
|-----|------|--------------------|-------------|
| `host` | String | — | File server hostname (leave unset to disable upload) |
| `port` | Int | `DEFAULT_UPLOAD_PORT` | File server port |
| `path` | String | `DEFAULT_UPLOAD_PATH` | Base path on the file server |

### `device` namespace

| Key | Type | Default (Config.h) | Description |
|-----|------|--------------------|-------------|
| `touch_pin` | Int | `DEFAULT_TOUCH_PIN` | Touch-sensitive GPIO number |
| `touch_thresh` | Int | `DEFAULT_TOUCH_THRESHOLD` | Touch detection threshold |
| `auto_transcribe` | Bool | `true` | Run transcription after each session |
| `auto_upload` | Bool | `false` | Upload files after transcription |

All NVS key names are defined in `Config.h` under the `NVS_KEY_*` constants. Compile-time defaults in `Config.h` are the last-resort fallback when no NVS value exists for a key.

---

*Built with ❤️*
