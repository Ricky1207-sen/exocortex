#include "AIClient.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "mbedtls/base64.h"

// Notes prompt — no transcription mention, direct audio understanding
static const char* NOTES_PROMPT = R"(
You are a personal memory assistant. Listen to this audio recording and generate clean, structured Markdown notes directly from what you hear. Do NOT transcribe — just understand and summarize.

Format your response as:

# Notes

## Summary
One or two sentences summarizing what was discussed.

## Key Points
- Important points mentioned

## Action Items
- [ ] Tasks or follow-ups mentioned (if any)

## People & Places
Names, organizations, or locations mentioned (if any)

---
Keep it concise. Skip empty sections. Plain Markdown only.
)";

// Network timeouts (ms). HTTPClient owns the actual reads; these just tell it
// how patient to be. The AI may think for a long time before it starts replying.
static const uint16_t AI_CONNECT_TIMEOUT_MS  = 15000;
static const uint16_t AI_RESPONSE_TIMEOUT_MS = 60000;

AIClient::AIClient() : _provider(PROVIDER_UNKNOWN) {}

bool AIClient::begin() {
    _apiKey   = String(AI_API_KEY);
    _provider = _detectProvider(_apiKey);

    if (_provider == PROVIDER_UNKNOWN) {
        DLOG("[AI] ERROR: Cannot detect provider from API key!");
        DLOG("[AI] Key must start with: sk- (OpenAI), AIza (Gemini), gsk_ (Groq)");
        return false;
    }

    DLOGF("[AI] Provider detected: %s\n", providerName());
    return true;
}

AIProvider AIClient::_detectProvider(const String& key) {
    if (key.startsWith("sk-"))   return PROVIDER_OPENAI;
    if (key.startsWith("AIza"))  return PROVIDER_GEMINI;
    if (key.startsWith("gsk_"))  return PROVIDER_GROQ;
    return PROVIDER_UNKNOWN;
}

const char* AIClient::providerName() const {
    switch (_provider) {
        case PROVIDER_OPENAI: return "OpenAI";
        case PROVIDER_GEMINI: return "Gemini";
        case PROVIDER_GROQ:   return "Groq";
        default:              return "Unknown";
    }
}

String AIClient::generateNotes(const String& wavPath) {
    DLOGF("[AI] Generating notes for: %s\n", wavPath.c_str());

    switch (_provider) {
        case PROVIDER_OPENAI: return _notesOpenAI(wavPath);
        case PROVIDER_GEMINI: return _notesGemini(wavPath);
        case PROVIDER_GROQ:   return _notesGroq(wavPath);
        default:
            DLOG("[AI] Unknown provider!");
            return "";
    }
}

// ── Escape a string for embedding in a JSON string value ───
String AIClient::_jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 16);
    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = (uint8_t)s[i];        // unsigned — UTF-8 bytes stay >= 0x80
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {           // all other control chars -> \u00XX
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
                break;
        }
    }
    return out;
}

// ── The escaped prompt, built once and reused ──────────────
const String& AIClient::_escapedPrompt() {
    static const String esc = _jsonEscape(NOTES_PROMPT);   // computed on first use
    return esc;
}

// ============================================================
// BodyStream — feeds the request body to HTTPClient piece by piece.
//
// A 160KB WAV becomes ~213KB of base64, which will not fit in RAM. HTTPClient
// can take any Stream as the body and pull from it as it sends, so instead of
// building the body we hand it this and let it ask for the next few KB at a
// time. Only one chunk is ever in memory.
//
// The body it produces is:  prefix + <file bytes> + suffix
// where the file is either base64-encoded (JSON APIs) or sent raw (multipart).
//
// This is the only reason the class exists — nothing else uses it.
// ============================================================
namespace {

const size_t RAW_CHUNK = 3072;                    // multiple of 3 → whole base64 blocks
const size_t ENC_CHUNK = RAW_CHUNK / 3 * 4 + 8;   // encoded output + null terminator

class BodyStream : public Stream {
public:
    BodyStream(File& file, const String& prefix, const String& suffix, bool asBase64)
        : _file(file), _prefix(prefix), _suffix(suffix), _asBase64(asBase64),
          _raw(nullptr), _enc(nullptr), _encLen(0), _encPos(0),
          _served(0), _failed(false)
    {
        // base64 length (with padding) = 4 output chars per 3 input bytes.
        size_t fileBytes = _file.size();
        size_t sentBytes = _asBase64 ? 4 * ((fileBytes + 2) / 3) : fileBytes;

        _prefixEnd = _prefix.length();
        _fileEnd   = _prefixEnd + sentBytes;
        _total     = _fileEnd + _suffix.length();

        _enc = (uint8_t*)malloc(ENC_CHUNK);
        if (_asBase64) _raw = (uint8_t*)malloc(RAW_CHUNK);
    }

    ~BodyStream() { free(_raw); free(_enc); }

    // False if the chunk buffers could not be allocated — check before sending,
    // otherwise we would promise a Content-Length we cannot deliver.
    bool ready() const { return _enc && (!_asBase64 || _raw); }

    // The exact Content-Length of the body we are about to produce.
    size_t totalSize() const { return _total; }

    // ── Stream interface (this is what HTTPClient calls) ────
    //
    // Bytes we still owe the caller. Returning -1 is the documented way to tell
    // HTTPClient to stop early: we use it if the SD read fails, so it aborts the
    // request rather than spinning forever waiting for bytes that never arrive.
    int available() override { return _failed ? -1 : (int)(_total - _served); }

    size_t readBytes(char* dst, size_t len) override {
        size_t done = 0;
        while (done < len) {
            size_t n = _next(dst + done, len - done);
            if (n == 0) break;
            done += n;
        }
        return done;
    }

    int read() override {
        char c;
        return readBytes(&c, 1) == 1 ? (uint8_t)c : -1;
    }

    // Not used: HTTPClient only ever calls available() and readBytes() on a
    // request body, and a streamed source cannot cheaply look ahead.
    int peek() override { return -1; }

    void flush() override {}
    size_t write(uint8_t) override { return 0; }   // read-only source

private:
    // Hand over up to `len` bytes from whichever section we are currently in.
    // Returns 0 when the body is finished (or could not be finished).
    size_t _next(char* dst, size_t len) {
        if (_served < _prefixEnd) return _copy(dst, len, _prefix, _served);
        if (_served < _fileEnd) {
            if (_encPos >= _encLen && !_refill()) {
                _failed = true;          // tells available() to abort the request
                return 0;
            }
            size_t n = _encLen - _encPos;
            if (n > len) n = len;
            memcpy(dst, _enc + _encPos, n);
            _encPos += n;
            _served += n;
            return n;
        }
        if (_served < _total) return _copy(dst, len, _suffix, _served - _fileEnd);
        return 0;
    }

    size_t _copy(char* dst, size_t len, const String& src, size_t from) {
        size_t n = src.length() - from;
        if (n > len) n = len;
        memcpy(dst, src.c_str() + from, n);
        _served += n;
        return n;
    }

    // Pull the next block from the file, base64-encoding it if needed.
    bool _refill() {
        _encLen = 0;
        _encPos = 0;
        if (!_file.available()) return false;

        if (!_asBase64) {
            int n = _file.read(_enc, ENC_CHUNK);
            if (n <= 0) return false;
            _encLen = n;
            return true;
        }

        // Fill a WHOLE block. Only the final block may be short, and that is
        // where base64 padding ('=') belongs — a short read mid-file would pad
        // in the middle of the stream and corrupt the audio.
        size_t filled = 0;
        while (filled < RAW_CHUNK && _file.available()) {
            int n = _file.read(_raw + filled, RAW_CHUNK - filled);
            if (n <= 0) break;
            filled += n;
        }
        if (filled == 0) return false;
        if (filled % 3 != 0 && _file.available()) {
            DLOG("[AI] Short read mid-file — aborting (would corrupt base64)");
            return false;
        }

        size_t olen = 0;
        if (mbedtls_base64_encode(_enc, ENC_CHUNK, &olen, _raw, filled) != 0) {
            DLOG("[AI] base64 encode failed");
            return false;
        }
        _encLen = olen;
        return true;
    }

    File&        _file;
    const String _prefix;
    const String _suffix;
    bool         _asBase64;

    uint8_t* _raw;      // one block straight off the SD card (base64 mode only)
    uint8_t* _enc;      // that block encoded, or the raw block itself
    size_t   _encLen;   // valid bytes in _enc
    size_t   _encPos;   // how many of them we've handed over

    size_t _prefixEnd;  // body offsets: [0,_prefixEnd) prefix,
    size_t _fileEnd;    // [_prefixEnd,_fileEnd) file, [_fileEnd,_total) suffix
    size_t _total;
    size_t _served;     // bytes handed over so far
    bool   _failed;
};

}  // namespace

// ── The one HTTP path ──────────────────────────────────────
// Sends bodyPrefix + <file> + bodySuffix and returns the response body.
// Leave filePath empty to send just bodyPrefix (a plain body, no file).
// HTTPClient owns the sockets, headers, redirects and chunked decoding.
String AIClient::_post(const String& url, const String& contentType,
                       const String& bearerToken, const String& bodyPrefix,
                       const String& bodySuffix, const String& filePath,
                       bool fileAsBase64) {
    WiFiClientSecure client;
    client.setInsecure();          // no cert bundle on device — skip verification

    HTTPClient http;
    if (!http.begin(client, url)) {
        DLOG("[AI] http.begin failed");
        return "";
    }
    http.setConnectTimeout(AI_CONNECT_TIMEOUT_MS);
    http.setTimeout(AI_RESPONSE_TIMEOUT_MS);
    http.addHeader("Content-Type", contentType);
    if (bearerToken.length()) http.addHeader("Authorization", "Bearer " + bearerToken);

    int code;
    if (filePath.isEmpty()) {
        code = http.POST(bodyPrefix);          // small enough to hold in RAM
    } else {
        File f = SD.open(filePath.c_str(), FILE_READ);
        if (!f) {
            DLOGF("[AI] Cannot open file: %s\n", filePath.c_str());
            http.end();
            return "";
        }
        BodyStream body(f, bodyPrefix, bodySuffix, fileAsBase64);
        if (!body.ready()) {
            DLOG("[AI] Body stream buffer alloc failed");
            f.close();
            http.end();
            return "";
        }
        DLOGF("[AI] Streaming %u byte body...\n", (unsigned)body.totalSize());
        code = http.sendRequest("POST", &body, body.totalSize());
        f.close();
    }

    if (code <= 0) {               // never reached the server — there is no body
        DLOGF("[AI] Request failed: %s\n", http.errorToString(code).c_str());
        http.end();
        return "";
    }
    if (code != 200) DLOGF("[AI] HTTP status: %d\n", code);

    String reply = http.getString();   // error bodies are useful — read either way
    http.end();
    return reply;
}

// ── Convenience: a plain JSON body, no file ────────────────
String AIClient::_postJSON(const String& url, const String& bearerToken,
                           const String& body) {
    return _post(url, "application/json", bearerToken, body, "", "", false);
}

// ── OpenAI — gpt-4o-audio-preview ─────────────────────────
String AIClient::_notesOpenAI(const String& wavPath) {
    String prefix = String("{\"model\":\"gpt-4o-audio-preview\",\"messages\":[")
        + "{\"role\":\"system\",\"content\":\"" + _escapedPrompt() + "\"},"
        + "{\"role\":\"user\",\"content\":["
        + "{\"type\":\"text\",\"text\":\"Generate notes from this audio recording.\"},"
        + "{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"";
    String suffix = "\",\"format\":\"wav\"}}]}],\"temperature\":0.3}";

    String response = _post("https://api.openai.com/v1/chat/completions",
                            "application/json", _apiKey,
                            prefix, suffix, wavPath, /*fileAsBase64=*/true);
    return _extractText(response, "OpenAI");
}

// ── Gemini — gemini-2.5-flash-lite ────────────────────────
String AIClient::_notesGemini(const String& wavPath) {
    String prefix = "{\"contents\":[{\"parts\":["
                    "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"";
    String suffix = String("\"}},{\"text\":\"") + _escapedPrompt() + "\"}]}]}";

    // Gemini authenticates with the key in the URL, not a Bearer header.
    String url = "https://generativelanguage.googleapis.com"
                 "/v1beta/models/gemini-2.5-flash-lite:generateContent?key=" + _apiKey;

    String response = _post(url, "application/json", "",
                            prefix, suffix, wavPath, /*fileAsBase64=*/true);
    return _extractText(response, "Gemini");
}

// ── Groq — Whisper transcribe + Llama notes ───────────────
String AIClient::_notesGroq(const String& wavPath) {
    // Groq has no direct audio → notes model, so: audio → transcript → notes.
    DLOG("[AI] Groq: Step 1 — transcribing...");

    // multipart/form-data — the WAV goes in raw (not base64) between these parts.
    const String boundary = "----MoneoBoundary";
    String head = "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    String tail = "\r\n--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
        "whisper-large-v3-turbo\r\n"
        "--" + boundary + "--\r\n";

    String transcriptResp = _post("https://api.groq.com/openai/v1/audio/transcriptions",
                                  "multipart/form-data; boundary=" + boundary,
                                  _apiKey, head, tail, wavPath, /*fileAsBase64=*/false);

    String transcript = _extractText(transcriptResp, "Groq transcript");
    if (transcript.isEmpty()) {
        DLOG("[AI] Empty transcript.");
        return "";
    }
    DLOGF("[AI] Groq transcript: %s\n", transcript.substring(0, 100).c_str());

    // Step 2 — generate notes from the transcript.
    DLOG("[AI] Groq: Step 2 — generating notes...");
    String body = String("{\"model\":\"llama3-70b-8192\",\"messages\":[")
        + "{\"role\":\"system\",\"content\":\"" + _escapedPrompt() + "\"},"
        + "{\"role\":\"user\",\"content\":\"Recording transcript:\\n\\n"
        + _jsonEscape(transcript) + "\"}"
        + "],\"temperature\":0.3}";

    String noteResp = _postJSON("https://api.groq.com/openai/v1/chat/completions",
                                _apiKey, body);
    return _extractText(noteResp, "Groq notes");
}

// ── Parse an AI reply and pull out the text ────────────────
// Handles the OpenAI/Groq shape (choices[0].message.content), the Gemini shape
// (candidates[0].content.parts[0].text), and the Whisper shape (text). Missing
// keys just yield nullptr, so trying each in turn is safe.
String AIClient::_extractText(const String& response, const char* provider) {
    if (response.isEmpty()) return "";

    // Keep only the fields we actually read, so the parsed document holds just
    // the text and not the surrounding metadata (Gemini sends a lot of it).
    StaticJsonDocument<512> filter;
    filter["choices"][0]["message"]["content"]             = true;  // OpenAI / Groq notes
    filter["candidates"][0]["content"]["parts"][0]["text"] = true;  // Gemini
    filter["text"]                                         = true;  // Whisper transcript
    filter["error"]["message"]                             = true;

    // Size the doc for the whole reply. The extracted text can be nearly all of
    // it (a Whisper reply is just {"text":"..."}), so anything smaller risks a
    // NoMemory failure that would silently throw the notes away.
    DynamicJsonDocument doc(response.length() + 1024);
    if (deserializeJson(doc, response,
                        DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
        DLOGF("[AI] JSON parse error. Response: %s\n", response.substring(0, 200).c_str());
        return "";
    }
    if (doc.containsKey("error")) {
        DLOGF("[AI] API error: %s\n", doc["error"]["message"] | "unknown");
        return "";
    }

    const char* text = doc["choices"][0]["message"]["content"];
    if (!text) text = doc["candidates"][0]["content"]["parts"][0]["text"];
    if (!text) text = doc["text"];              // Whisper transcription
    if (!text) {
        DLOGF("[AI] No text in %s response.\n", provider);
        return "";
    }
    DLOGF("[AI] Extracted %d chars from %s\n", (int)strlen(text), provider);
    return String(text);
}                               
