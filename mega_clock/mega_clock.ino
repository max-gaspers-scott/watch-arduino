// Mega side: shows the time on an I2C LCD.
// Gets the time from the ESP8266 (on Serial3) every second. If there is no
// WiFi (or the ESP stops talking), it keeps counting on its own, one second
// per second, until good time arrives again.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);  // change 0x27 / 16,2 if your display differs

int hh = 0, mm = 0, ss = 0;
bool haveTime = false;      // true once we have received the time at least once
bool online = false;        // true while the ESP reports WiFi + valid time
unsigned long lastTick = 0;     // last time we advanced the local clock
unsigned long lastGoodMsg = 0;  // last time we got a valid time from the ESP

char line[32];
byte lineLen = 0;

void showTime() {
  char buf[17];
  lcd.setCursor(0, 0);
  if (haveTime) {
    snprintf(buf, sizeof(buf), "    %02d:%02d:%02d    ", hh, mm, ss);
  } else {
    snprintf(buf, sizeof(buf), "    --:--:--    ");
  }
  lcd.print(buf);

  lcd.setCursor(0, 1);
  lcd.print(online ? "    WiFi OK     " : "    No WiFi     ");
}

void addOneSecond() {
  if (++ss >= 60) { ss = 0;
    if (++mm >= 60) { mm = 0;
      if (++hh >= 24) hh = 0;
    }
  }
}

void handleLine(const char* s) {
  int h, m, sec;
  if (s[0] == 'T' && sscanf(s, "T %d:%d:%d", &h, &m, &sec) == 3 &&
      h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0 && sec < 60) {
    hh = h; mm = m; ss = sec;
    haveTime = true;
    online = true;
    lastGoodMsg = millis();
    lastTick = millis();  // restart the local 1 s count from this fresh time
    showTime();
  } else if (s[0] == 'N' && s[1] == '\0') {
    online = false;
  }
  // anything else (ESP boot noise etc.) is ignored
}

void readEsp() {
  while (Serial3.available()) {
    char c = Serial3.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        line[lineLen] = '\0';
        handleLine(line);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    } else {
      lineLen = 0;  // garbage / too long, drop it
    }
  }
}

void setup() {
  Serial3.begin(115200);  // link to the ESP8266
  lcd.init();
  lcd.backlight();
  showTime();
  lastTick = millis();
}

void loop() {
  readEsp();

  // If the ESP hasn't sent a good time for 3 s, treat it as offline.
  if (online && millis() - lastGoodMsg > 3000) online = false;

  // Every second: if we're not getting time from WiFi, count it ourselves.
  if (millis() - lastTick >= 1000) {
    lastTick += 1000;
    if (!online && haveTime) addOneSecond();
    showTime();
  }
}
