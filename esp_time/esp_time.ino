// ESP8266 side: gets time from NTP over WiFi and sends it to the Mega
// once per second over the internal serial link (Mega's Serial3).
//
// Sends one line per second:
//   "T hh:mm:ss"  -> WiFi connected and time is synced
//   "N"           -> no WiFi (or time not synced yet)

#include <ESP8266WiFi.h>
#include <time.h>

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// POSIX timezone string (handles daylight saving automatically). Examples:
//   UTC:                "UTC0"
//   UK:                 "GMT0BST,M3.5.0/1,M10.5.0"
//   Central Europe:     "CET-1CEST,M3.5.0,M10.5.0/3"
//   US Eastern:         "EST5EDT,M3.2.0,M11.1.0"
//   US Central:         "CST6CDT,M3.2.0,M11.1.0"
//   US Pacific:         "PST8PDT,M3.2.0,M11.1.0"
const char* TIMEZONE = "EST5EDT,M3.2.0,M11.1.0";

unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);  // this is wired to the Mega's Serial3
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
}

void loop() {
  if (millis() - lastSend >= 1000) {
    lastSend += 1000;

    time_t now = time(nullptr);
    if (WiFi.status() == WL_CONNECTED && now > 1700000000) {  // valid, synced time
      struct tm t;
      localtime_r(&now, &t);
      char buf[16];
      snprintf(buf, sizeof(buf), "T %02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
      Serial.println(buf);
    } else {
      Serial.println("N");
    }
  }
}
