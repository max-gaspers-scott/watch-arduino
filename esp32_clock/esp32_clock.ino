// ESP32 binary clock: gets the time from the internet (NTP) over WiFi and
// shows it on a 20x4 I2C LCD, in binary:
//   row 1: hours   (e.g. 8  -> 1000)
//   row 2: minutes (e.g. 30 -> 11110)
//   row 3: seconds
//   row 4: a scrolling wave animation
//
// The right side of rows 1-3 shows the WiFi status. It is blank when
// everything is working, and says what went wrong if WiFi fails (wrong
// password, network not found, no internet, ...).
//
// Once the time has been synced, the ESP32 keeps counting on its own clock,
// so the time keeps going even if WiFi drops later.
//
// Wiring (default ESP32 I2C pins):
//   LCD SDA -> GPIO 21
//   LCD SCL -> GPIO 22
//   LCD VCC -> 5V (VIN)   LCD GND -> GND
//
// Needs the "LiquidCrystal I2C" library (same one as the Mega version).

#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
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

const int I2C_SDA = 21;
const int I2C_SCL = 22;

const int LCD_COLS = 20;
const int LCD_ROWS = 4;
LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS);  // change 0x27 if your display differs

// Set to true to print what's happening to the Serial Monitor (115200 baud).
const bool DEBUG = true;

// How long to wait at each step before calling it an error.
const unsigned long CONNECT_TIMEOUT_MS = 20000;  // find router + log in
const unsigned long DHCP_TIMEOUT_MS    = 10000;  // get an IP address
const unsigned long NTP_TIMEOUT_MS     = 15000;  // get the time from the internet
const unsigned long RETRY_DELAY_MS     = 10000;  // wait after an error before retrying

// ---- LCD layout ----
// Columns 0-5: binary number (max 6 bits), column 6: gap,
// columns 7-19: status text (13 characters, one line per row).
const int BIN_WIDTH = 7;
const int STATUS_COL = BIN_WIDTH;
const int STATUS_WIDTH = LCD_COLS - STATUS_COL;  // 13

char statusText[3][STATUS_WIDTH + 1];

void setStatus(const char* a, const char* b = "", const char* c = "") {
  const char* lines[3] = {a, b, c};
  for (int i = 0; i < 3; i++) {
    snprintf(statusText[i], sizeof(statusText[i]), "%s", lines[i]);
  }
  if (DEBUG) Serial.printf("LCD status: %s | %s | %s\n", a, b, c);
}

// ---- Time ----

bool timeIsValid() {
  return time(nullptr) > 1700000000;  // anything before Nov 2023 means "not synced"
}

// Prints one LCD row: the number in binary (no leading zeros), or "--" if
// we don't know the time yet, followed by that row's status text.
void printRow(int row, int value, bool haveTime) {
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
  while (len < STATUS_COL) buf[len++] = ' ';
  for (const char* p = statusText[row]; *p && len < LCD_COLS; p++) buf[len++] = *p;
  while (len < LCD_COLS) buf[len++] = ' ';
  buf[len] = '\0';
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void showTime() {
  bool haveTime = timeIsValid();
  struct tm t = {};
  if (haveTime) {
    time_t now = time(nullptr);
    localtime_r(&now, &t);
  }
  printRow(0, t.tm_hour, haveTime);
  printRow(1, t.tm_min, haveTime);
  printRow(2, t.tm_sec, haveTime);
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

// ---- WiFi ----
// The WiFi events arrive on another task, so they just set flags here and
// loop() deals with them.
volatile bool evAssociated = false;   // router accepted the password
volatile bool evGotIP = false;        // got an IP address
volatile bool evDisconnected = false; // attempt failed / connection dropped
volatile int  evReason = 0;           // why it disconnected

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      evAssociated = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      evGotIP = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      evReason = info.wifi_sta_disconnected.reason;
      evDisconnected = true;
      break;
    default:
      break;
  }
}

// Shows on the LCD why the router dropped us (3 lines, max 13 chars each).
//                                    "-------------"  <- 13 chars
void showDisconnectReason(int r) {
  switch (r) {
    case 15:   // 4-way handshake timeout
    case 204:  // handshake timeout
      setStatus("WiFi error:", "Wrong", "password");
      break;
    case 202:
      setStatus("WiFi error:", "Wrong pass or", "security mode");
      break;
    case 201:
      setStatus("No network", "named that.", "Check 2.4GHz");
      break;
    case 18: case 19: case 20: case 210:
      setStatus("WiFi error:", "Router mode", "unsupported");
      break;
    case 2:
      setStatus("WiFi error:", "Login timed", "out (signal?)");
      break;
    case 5:
      setStatus("WiFi error:", "Router full", "(too many)");
      break;
    case 23:
      setStatus("WiFi error:", "Enterprise", "not supported");
      break;
    case 200:
      setStatus("WiFi lost:", "Signal too", "weak/router");
      break;
    case 203:
      setStatus("WiFi error:", "Router", "rejected us");
      break;
    default: {
      char code[STATUS_WIDTH + 1];
      snprintf(code, sizeof(code), "reason %d", r);
      setStatus("WiFi error:", "Disconnected", code);
      break;
    }
  }
}

enum WifiStep { STEP_CONNECTING, STEP_GETTING_IP, STEP_GETTING_TIME, STEP_OK, STEP_WAIT_RETRY, STEP_NO_CONFIG };
WifiStep step = STEP_CONNECTING;
unsigned long stepStart = 0;
bool ntpChecked = false;     // already looked into why NTP isn't answering
bool hadError = false;       // keep the last error on screen while retrying

void goToStep(WifiStep s) {
  step = s;
  stepStart = millis();
}

void startConnect() {
  evAssociated = evGotIP = evDisconnected = false;
  if (!hadError) setStatus("Connecting", "to WiFi...");
  if (DEBUG) Serial.printf("Connecting to \"%s\"...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  goToStep(STEP_CONNECTING);
}

void failWith(bool stopRadio) {
  hadError = true;
  if (stopRadio) WiFi.disconnect();
  if (DEBUG) Serial.printf("Retrying in %lu s\n", RETRY_DELAY_MS / 1000);
  goToStep(STEP_WAIT_RETRY);
}

// Checks for a dropped connection; returns true if it happened.
bool checkDropped() {
  if (!evDisconnected) return false;
  evDisconnected = false;
  if (DEBUG) Serial.printf("Disconnected, reason %d\n", evReason);
  showDisconnectReason(evReason);
  failWith(false);
  return true;
}

void updateWifi() {
  unsigned long elapsed = millis() - stepStart;

  switch (step) {
    case STEP_NO_CONFIG:
      break;  // nothing to do until the code is fixed

    case STEP_CONNECTING:
      if (checkDropped()) break;
      if (evAssociated) {
        if (DEBUG) Serial.println("Password accepted, waiting for IP address...");
        if (!hadError) setStatus("Getting IP", "address...");
        goToStep(STEP_GETTING_IP);
      } else if (elapsed > CONNECT_TIMEOUT_MS) {
        setStatus("WiFi error:", "No answer", "from router");
        failWith(true);
      }
      break;

    case STEP_GETTING_IP:
      if (checkDropped()) break;
      if (evGotIP) {
        if (DEBUG) Serial.printf("Got IP %s\n", WiFi.localIP().toString().c_str());
        hadError = false;
        ntpChecked = false;
        if (timeIsValid()) setStatus("");
        else setStatus("Getting time", "from", "internet...");
        goToStep(STEP_GETTING_TIME);
      } else if (elapsed > DHCP_TIMEOUT_MS) {
        setStatus("WiFi error:", "No IP from", "router (DHCP)");
        failWith(true);
      }
      break;

    case STEP_GETTING_TIME:
      if (checkDropped()) break;
      if (timeIsValid()) {
        if (DEBUG) Serial.println("Time synced, all good");
        setStatus("");
        goToStep(STEP_OK);
      } else if (elapsed > NTP_TIMEOUT_MS && !ntpChecked) {
        // Connected to WiFi but no time. Find out if the internet works at all.
        // (Keep waiting afterwards: the ESP32 keeps asking the time servers.)
        ntpChecked = true;
        IPAddress ip;
        if (!WiFi.hostByName("pool.ntp.org", ip)) {
          setStatus("WiFi OK but", "no internet", "(DNS failed)");
        } else {
          setStatus("WiFi OK but", "time server", "not replying");
        }
      }
      break;

    case STEP_OK:
      checkDropped();  // time keeps running on the ESP32 clock if this happens
      break;

    case STEP_WAIT_RETRY:
      if (elapsed > RETRY_DELAY_MS) startConnect();
      break;
  }
}

void setup() {
  if (DEBUG) {
    Serial.begin(115200);
    delay(200);
    Serial.println("\nESP32 binary clock started");
  }

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  makeWaveChars();

  setenv("TZ", TIMEZONE, 1);
  tzset();

  WiFi.persistent(false);       // don't save credentials to flash every boot
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false); // we handle retries ourselves (see updateWifi)
  WiFi.onEvent(onWifiEvent);
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "YOUR_WIFI_NAME") == 0) {
    setStatus("Set WiFi name", "& password", "in the code");
    goToStep(STEP_NO_CONFIG);
  } else {
    startConnect();
  }

  showTime();
  drawWave();
}

void loop() {
  updateWifi();

  // Redraw the time whenever the second changes (or every 1 s while unsynced).
  static time_t lastShown = 0;
  static unsigned long lastDraw = 0;
  time_t now = time(nullptr);
  if (now != lastShown || millis() - lastDraw >= 1000) {
    lastShown = now;
    lastDraw = millis();
    showTime();
  }

  if (millis() - lastWave >= WAVE_MS) {
    lastWave = millis();
    drawWave();
  }
}
