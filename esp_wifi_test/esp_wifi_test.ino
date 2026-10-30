// ESP8266 WiFi test: scans for networks, tries to connect, and prints
// the result to Serial (115200 baud). Retries every 15 seconds on failure,
// and reports if the connection drops later.
//
// Each step of the connection is printed as it happens (searching,
// associated with router, got IP, or dropped + the router's reason code),
// so you can see exactly where it gets stuck.

#include <ESP8266WiFi.h>

const char* WIFI_SSID = "WIFI_NAME";
const char* WIFI_PASS = "WIFI_PASSWORD";

const unsigned long CONNECT_TIMEOUT_MS = 15000;

// Event handlers only stay registered while these objects exist.
WiFiEventHandler onConnectedHandler;
WiFiEventHandler onDisconnectedHandler;
WiFiEventHandler onGotIPHandler;
WiFiEventHandler onAuthModeHandler;

unsigned long attemptStart = 0;

const char* statusName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (network name not found)";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED (check password)";
    case WL_WRONG_PASSWORD:  return "WRONG_PASSWORD";
    case WL_DISCONNECTED:    return "DISCONNECTED (still trying, no definite answer)";
    default:                 return "UNKNOWN";
  }
}

// Low-level state of the ESP's connection state machine.
const char* stationStateName(station_status_t s) {
  switch (s) {
    case STATION_IDLE:           return "idle";
    case STATION_CONNECTING:     return "connecting (searching / authenticating / waiting for IP)";
    case STATION_WRONG_PASSWORD: return "wrong password";
    case STATION_NO_AP_FOUND:    return "network not found";
    case STATION_CONNECT_FAIL:   return "connect failed";
    case STATION_GOT_IP:         return "got IP";
    default:                     return "unknown";
  }
}

// Reason codes sent by the router (or the ESP) when a connection attempt drops.
const char* disconnectReason(int r) {
  switch (r) {
    case 1:   return "unspecified";
    case 2:   return "auth expired (router stopped answering during login)";
    case 3:   return "router kicked us off (auth leave)";
    case 4:   return "association expired";
    case 5:   return "router has too many clients";
    case 6:   return "not authenticated";
    case 7:   return "not associated";
    case 8:   return "association left";
    case 15:  return "4-way handshake timeout (usually WRONG PASSWORD)";
    case 16:  return "group key update timeout";
    case 18:  return "group cipher invalid (router security mode not supported)";
    case 19:  return "pairwise cipher invalid (router security mode not supported)";
    case 20:  return "AKMP invalid (router may be WPA3-only)";
    case 23:  return "802.1X auth failed (enterprise network)";
    case 200: return "beacon timeout (signal lost / too weak)";
    case 201: return "no AP found (name not visible, 5 GHz, or out of range)";
    case 202: return "auth failed (wrong password or security mode)";
    case 203: return "association failed (router rejected us)";
    case 204: return "handshake timeout (usually WRONG PASSWORD)";
    default:  return "other";
  }
}

const char* encryptionName(uint8_t e) {
  switch (e) {
    case ENC_TYPE_NONE: return "open";
    case ENC_TYPE_WEP:  return "WEP";
    case ENC_TYPE_TKIP: return "WPA";
    case ENC_TYPE_CCMP: return "WPA2";
    case ENC_TYPE_AUTO: return "WPA/WPA2";
    default:            return "unknown (WPA3?)";
  }
}

void printElapsed() {
  Serial.printf("  [%6lu ms] ", millis() - attemptStart);
}

void setupEventHandlers() {
  onConnectedHandler = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& e) {
    printElapsed();
    Serial.printf("Associated with router \"%s\" (channel %d, BSSID %02X:%02X:%02X:%02X:%02X:%02X)\n",
                  e.ssid.c_str(), e.channel,
                  e.bssid[0], e.bssid[1], e.bssid[2], e.bssid[3], e.bssid[4], e.bssid[5]);
    printElapsed();
    Serial.println("Password accepted, waiting for IP address (DHCP)...");
  });

  onDisconnectedHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
    printElapsed();
    Serial.printf("Dropped, reason %d: %s\n", (int)e.reason, disconnectReason((int)e.reason));
  });

  onGotIPHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& e) {
    printElapsed();
    Serial.print("Got IP address ");
    Serial.println(e.ip);
  });

  onAuthModeHandler = WiFi.onStationModeAuthModeChanged([](const WiFiEventStationModeAuthModeChanged& e) {
    printElapsed();
    Serial.printf("Router changed security mode: %d -> %d\n", e.oldMode, e.newMode);
  });
}

void scanNetworks() {
  Serial.println("Scanning for networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("  No networks found");
    return;
  }
  bool found = false;
  for (int i = 0; i < n; i++) {
    bool isTarget = WiFi.SSID(i) == WIFI_SSID;
    if (isTarget) found = true;
    Serial.printf("  %2d: %-32s  %4d dBm  ch %2d  %-15s %s\n",
                  i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  encryptionName(WiFi.encryptionType(i)),
                  isTarget ? "<-- target" : "");
  }
  if (!found) {
    Serial.printf("  WARNING: \"%s\" was not in the scan. Check spelling/case,\n", WIFI_SSID);
    Serial.println("  and that it is a 2.4 GHz network (ESP8266 can't see 5 GHz).");
  }
}

bool connectWifi() {
  Serial.printf("Connecting to \"%s\"...\n", WIFI_SSID);
  attemptStart = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Print the low-level state each time it changes, plus a heartbeat every
  // few seconds so you know it hasn't frozen.
  station_status_t lastState = (station_status_t)-1;
  unsigned long lastHeartbeat = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - attemptStart < CONNECT_TIMEOUT_MS) {
    station_status_t state = wifi_station_get_connect_status();
    if (state != lastState) {
      printElapsed();
      Serial.printf("State: %s\n", stationStateName(state));
      lastState = state;
    } else if (millis() - lastHeartbeat >= 3000) {
      printElapsed();
      Serial.println("...still waiting");
    }
    if (millis() - lastHeartbeat >= 3000) lastHeartbeat = millis();
    delay(100);
  }

  wl_status_t s = WiFi.status();
  if (s != WL_CONNECTED) {
    Serial.printf("Failed after %lu ms, status: %s\n", millis() - attemptStart, statusName(s));
    return false;
  }

  Serial.printf("Connected to Wi-Fi in %lu ms!\n", millis() - attemptStart);
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

  WiFi.persistent(false);  // don't reuse old credentials saved in flash
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  setupEventHandlers();

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
