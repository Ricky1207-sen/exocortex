#ifndef AIClient_h
#define AIClient_h

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include "Config.h"

// ============================================================
// AIClient — Detects AI provider from API key prefix,
// sends WAV file directly to API, returns Markdown notes.
//
// No gateway needed. ESP32 calls API directly.
// No transcription — direct audio → notes (saves tokens).
//
// Supported:
//   sk-...   → OpenAI  gpt-4o-audio-preview
//   AIza...  → Gemini  gemini-2.5-flash-lite
//   gsk_...  → Groq    (transcribe + notes, two calls)
// ============================================================

enum AIProvider { PROVIDER_OPENAI, PROVIDER_GEMINI, PROVIDER_GROQ, PROVIDER_UNKNOWN };

class AIClient {
public:
    AIClient();
    bool begin();

    // Send WAV file, get back markdown notes
    String generateNotes(const String& wavPath);

    const char* providerName() const;

private:
    AIProvider _detectProvider(const String& key);

    String _notesOpenAI(const String& wavPath);
    String _notesGemini(const String& wavPath);
    String _notesGroq(const String& wavPath);   // transcribe + notes

    // Escape a string so it can be embedded inside a JSON string value.
    // Static + pure — doesn't touch any member.
    static String _jsonEscape(const String& s);
    // The notes prompt, escaped once and cached (it never changes).
    static const String& _escapedPrompt();
    // Parse an AI JSON reply and pull out the text (all API shapes).
    String _extractText(const String& response, const char* provider);

    // The one HTTP path. Sends a body of bodyPrefix + <file> + bodySuffix and
    // returns the response body. The file is streamed, so it is never held
    // whole in RAM. Leave filePath empty to send just bodyPrefix.
    String _post(const String& url, const String& contentType,
                 const String& bearerToken, const String& bodyPrefix,
                 const String& bodySuffix, const String& filePath,
                 bool fileAsBase64);
    // Convenience wrapper over _post: a plain JSON body, no file.
    String _postJSON(const String& url, const String& bearerToken,
                     const String& body);

    AIProvider _provider;
    String     _apiKey;
};

#endif
