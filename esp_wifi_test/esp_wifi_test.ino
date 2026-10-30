// ESP8266 WiFi test: scans for networks, tries to connect, and prints
// the result to Serial (115200 baud). Retries every 15 seconds on failure,
// and reports if the connection drops later.

#include <ESP8266WiFi.h>

const char* WIFI_SSID = "WIFI_NAME";
const char* WIFI_PASS = "WIFI_PASSWORD";

const unsigned long CONNECT_TIMEOUT_MS = 15000;

const char* statusName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (network name not found)";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED (check password)";
    case WL_WRONG_PASSWORD:  return "WRONG_PASSWORD";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

void scanNetworks() {
  Serial.println("Scanning for networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("  No networks found");
    return;
  }
  for (int i = 0; i < n; i++) {
    Serial.printf("  %2d: %-32s  %4d dBm  ch %2d  %s\n",
                  i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "secured");
  }
}

bool connectWifi() {
  Serial.printf("Connecting to \"%s\"", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  wl_status_t s = WiFi.status();
  if (s != WL_CONNECTED) {
    Serial.printf("Failed after %lu ms, status: %s\n", millis() - start, statusName(s));
    return false;
  }

  Serial.printf("Connected to Wi-Fi in %lu ms!\n", millis() - start);
  Serial.print("  IP address: "); Serial.println(WiFi.localIP());
  Serial.print("  Gateway:    "); Serial.println(WiFi.gatewayIP());
  Serial.print("  DNS:        "); Serial.println(WiFi.dnsIP());
  Serial.print("  MAC:        "); Serial.println(WiFi.macAddress());
  Serial.printf("  Signal:     %d dBm\n", WiFi.RSSI());

  IPAddress ip;
  if (WiFi.hostByName("pool.ntp.org", ip)) {
    Serial.print("  Internet OK (pool.ntp.org -> "); Serial.print(ip); Serial.println(")");
  } else {
    Serial.println("  Connected to WiFi but DNS lookup failed (no internet?)");
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP8266 WiFi test ===");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  scanNetworks();
  while (!connectWifi()) {
    Serial.println("Retrying in 15 seconds...\n");
    delay(15000);
  }
}

void loop() {
  static unsigned long lastReport = 0;
  if (millis() - lastReport >= 10000) {
    lastReport = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Still connected, signal %d dBm\n", WiFi.RSSI());
    } else {
      Serial.printf("Connection lost, status: %s. Reconnecting...\n", statusName(WiFi.status()));
      connectWifi();
    }
  }
}
