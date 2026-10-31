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

// Set to true to print what's happening to the USB Serial Monitor (115200 baud).
// Needs DIP switches 1, 2, 3, 4 ON so you can see it while the ESP is connected.
const bool DEBUG = true;

char line[32];
byte lineLen = 0;

// Decorative wave on the bottom row while WiFi is OK. Uses the LCD's 8 custom
// characters as bars of height 1..8 pixels, and scrolls a sine-like shape.
const unsigned long WAVE_FRAME_MS = 120;  // lower = faster wave
const byte WAVE[16] = {4, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 2, 1, 1, 2, 3};
byte waveFrame = 0;
unsigned long lastWave = 0;

void createWaveChars() {
  for (byte h = 1; h <= 8; h++) {
    byte bar[8];
    for (byte row = 0; row < 8; row++) {
      bar[row] = (row >= 8 - h) ? 0x1F : 0x00;  // fill from the bottom up
    }
    lcd.createChar(h - 1, bar);
  }
}

void drawWave() {
  lcd.setCursor(0, 1);
  for (byte col = 0; col < 16; col++) {
    lcd.write(byte(WAVE[(col + waveFrame) % 16] - 1));
  }
  waveFrame = (waveFrame + 1) % 16;
}

void showTime() {
  char buf[17];
  lcd.setCursor(0, 0);
  if (haveTime) {
    snprintf(buf, sizeof(buf), "    %02d:%02d:%02d    ", hh, mm, ss);
  } else {
    snprintf(buf, sizeof(buf), "    --:--:--    ");
  }
  lcd.print(buf);

  // When online, the bottom row is the wave (drawn in loop()).
  if (!online) {
    lcd.setCursor(0, 1);
    lcd.print("    No WiFi     ");
  }
}

void addOneSecond() {
  if (++ss >= 60) { ss = 0;
    if (++mm >= 60) { mm = 0;
      if (++hh >= 24) hh = 0;
    }
  }
}

void handleLine(const char* s) {
  if (DEBUG) { Serial.print("ESP says: "); Serial.println(s); }
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
  if (DEBUG) {
    Serial.begin(115200);
    Serial.println("Mega started");  // if this repeats, the Mega keeps resetting
  }
  Serial3.begin(115200);  // link to the ESP8266
  lcd.init();
  lcd.backlight();
  createWaveChars();
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

  if (online && millis() - lastWave >= WAVE_FRAME_MS) {
    lastWave = millis();
    drawWave();
  }
}
