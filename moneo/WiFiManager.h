#ifndef WiFiManager_h
#define WiFiManager_h

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include "Config.h"

// ============================================================
// WiFiManager — wraps WiFiMulti to connect to the strongest
// known AP. Networks are provisioned in Config.h.
// ============================================================

class WiFiManager {
public:
    WiFiManager();
    bool begin();              // register provisioned networks with WiFiMulti
    bool connect();            // bring up the best available known network
    bool disconnect();         // drop the link (call before recording starts)
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    String currentSSID() const { return WiFi.SSID(); }
    String localIP() const { return WiFi.localIP().toString(); }

private:
    WiFiMulti _wifiMulti;
    bool      _begun;
};

#endif
