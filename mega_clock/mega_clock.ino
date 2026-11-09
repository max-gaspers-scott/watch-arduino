// Mega side: shows the time on a 20x4 I2C LCD, in binary:
//   row 1: hours   (e.g. 8  -> 1000)
//   row 2: minutes (e.g. 30 -> 11110)
//   row 3: seconds
//   row 4: a scrolling wave animation
// Gets the time from the ESP8266 (on Serial3) every second. If there is no
// WiFi (or the ESP stops talking), it keeps counting on its own, one second
// per second, until good time arrives again.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const int LCD_COLS = 20;
const int LCD_ROWS = 4;
LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS);  // change 0x27 if your display differs

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

// Prints one LCD row: the number in binary (no leading zeros), or "--" if
// we don't know the time yet. The rest of the row is blanked out.
void printBinaryRow(int row, int value) {
  char buf[LCD_COLS + 1];
  int len = 0;
  if (!haveTime) {
    buf[len++] = '-'; buf[len++] = '-';
  } else if (value == 0) {
    buf[len++] = '0';
  } else {
    int bit = 5;                           // 63 is the biggest value we show
    while (!(value & (1 << bit))) bit--;   // skip leading zeros
    for (; bit >= 0; bit--) buf[len++] = (value & (1 << bit)) ? '1' : '0';
  }
  while (len < LCD_COLS) buf[len++] = ' ';
  buf[len] = '\0';
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void showTime() {
  printBinaryRow(0, hh);
  printBinaryRow(1, mm);
  printBinaryRow(2, ss);
}

// ---- Wave animation (bottom row) ----
// 8 custom characters: bars of height 1..8 pixels.
void makeWaveChars() {
  for (byte level = 0; level < 8; level++) {
    byte bars[8];
    for (byte r = 0; r < 8; r++) bars[r] = (r >= 7 - level) ? 0x1F : 0x00;
    lcd.createChar(level, bars);
  }
}

// One full sine cycle, as bar heights 0..7 (16 steps).
const byte WAVE[16] = {4, 5, 6, 7, 7, 7, 6, 5, 4, 2, 1, 0, 0, 0, 1, 2};
byte wavePhase = 0;
unsigned long lastWave = 0;
const unsigned long WAVE_MS = 120;  // lower = faster wave

void drawWave() {
  lcd.setCursor(0, 3);
  for (int col = 0; col < LCD_COLS; col++) {
    lcd.write((uint8_t)WAVE[(col + wavePhase) % 16]);
  }
  wavePhase = (wavePhase + 1) % 16;
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
  makeWaveChars();
  showTime();
  drawWave();
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

  if (millis() - lastWave >= WAVE_MS) {
    lastWave = millis();
    drawWave();
  }
}
