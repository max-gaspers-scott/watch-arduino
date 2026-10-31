// ESP8266 side: gets time from NTP over WiFi and shows it directly on the
// I2C LCD (no Mega needed). Once the time has synced, the ESP keeps counting
// on its own if WiFi drops, and re-syncs when WiFi comes back.
//
// Still sends one line per second on Serial (115200 baud), so you can watch
// it in the Serial Monitor (DIP 5, 6 ON) or let a Mega read it later:
//   "T hh:mm:ss"  -> WiFi connected and time is synced
//   "N"           -> no WiFi (or time not synced yet)

#include <ESP8266WiFi.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

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

// ESP8266 GPIO numbers the LCD's SDA / SCL wires are plugged into.
// GPIO4 / GPIO5 are the ESP8266's standard I2C pins. Avoid GPIO1, 3 (serial),
// 6-11 (flash chip) and 15 (must be low at boot).
const int LCD_SDA = 4;
const int LCD_SCL = 5;

LiquidCrystal_I2C lcd(0x27, 16, 2);  // change 0x27 / 16,2 if your display differs

unsigned long lastSend = 0;

void showTime(bool synced, bool wifiOk, const struct tm& t) {
  char buf[17];
  lcd.setCursor(0, 0);
  if (synced) {
    snprintf(buf, sizeof(buf), "    %02d:%02d:%02d    ", t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    snprintf(buf, sizeof(buf), "    --:--:--    ");
  }
  lcd.print(buf);

  lcd.setCursor(0, 1);
  lcd.print(wifiOk ? "    WiFi OK     " : "    No WiFi     ");
}

void setup() {
  Serial.begin(115200);

  Wire.begin(LCD_SDA, LCD_SCL);  // must come before lcd.init()
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("  Connecting... ");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
}

void loop() {
  if (millis() - lastSend >= 1000) {
    lastSend += 1000;

    time_t now = time(nullptr);
    bool synced = now > 1700000000;  // false until the first NTP sync
    bool wifiOk = WiFi.status() == WL_CONNECTED;
    struct tm t;
    localtime_r(&now, &t);

    showTime(synced, wifiOk, t);

    if (wifiOk && synced) {
      char buf[16];
      snprintf(buf, sizeof(buf), "T %02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
      Serial.println(buf);
    } else {
      Serial.println("N");
    }
  }
}
