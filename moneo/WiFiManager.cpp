#include "WiFiManager.h"

// Add networks to WIFI_NETWORKS in Config.h. Do NOT commit real credentials.
// WiFiMulti selects the strongest available AP; entry order doesn't imply priority.
const char* WIFI_NETWORKS[WIFI_NETWORK_COUNT][2] = {
    { "YOUR_WIFI_SSID",     "YOUR_WIFI_PASSWORD"   },
    { "SECOND_WIFI_SSID",   "SECOND_WIFI_PASSWORD" },   // e.g. laptop hotspot
};

WiFiManager::WiFiManager() : _begun(false) {}

bool WiFiManager::begin() {
    if (_begun) return true;

    for (int i = 0; i < WIFI_NETWORK_COUNT; i++) {
        _wifiMulti.addAP(WIFI_NETWORKS[i][0], WIFI_NETWORKS[i][1]);
    }
    _begun = true;
    DLOGF("[WiFi] %d network(s) provisioned.\n", WIFI_NETWORK_COUNT);
    return true;
}

bool WiFiManager::connect() {
    if (!_begun) begin();

    DLOG("[WiFi] Connecting...");
    if (_wifiMulti.run(WIFI_CONNECT_TIMEOUT_MS) == WL_CONNECTED) {
        DLOGF("[WiFi] Connected to %s — IP: %s\n",
              WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        return true;
    }

    DLOG("[WiFi] No known network available.");
    return false;
}

bool WiFiManager::disconnect() {
    WiFi.disconnect();
    DLOG("[WiFi] Disconnected.");
    return true;
}
