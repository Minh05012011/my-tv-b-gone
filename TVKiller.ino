/*
  ============================================================================
  TV KILLER PRO v3 - ESP8266 (1 nut bam dieu khien tat ca)
  ============================================================================
  Phan cung:
    - OLED SSD1306 128x32          SDA=D2  SCL=D1
    - Nut bam                      D3 (INPUT_PULLUP, nhan xuong GND)
    - LED WS2812B (1 con)          D6
    - IR LED phat (qua transistor) D5 HOAC D7 (chon trong Settings)
    - Mat thu IR (VS1838B/TL1838)  D4

  Thu vien can cai:
    - IRremoteESP8266  >= 2.8.6
    - Adafruit GFX Library
    - Adafruit SSD1306
    - Adafruit NeoPixel
    - ArduinoJson v6.x
    - ESP8266 core (LittleFS, WiFi, WebServer)

  ----------------------------------------------------------------------------
  LOGIC NUT BAM:
    - NHAN NGAN          = CHON / XAC NHAN
    - GIU (~0.6s)        = CHUYEN MUC KE TIEP (auto-scroll khi giu tiep)
    - GIU SIEU LAU (>3s) = Quay ve MENU CHINH (bat ky man hinh nao)
  ----------------------------------------------------------------------------

  MENU CHINH (7 muc):
    0. TV-B-GONE   : Sub-menu chon che do ban ma
    1. PHAT IR LUU : Phat lai ma da luu
    2. LUU IR MOI  : Hoc ma tu remote
    3. WIFI WEB    : Bat Access Point + web quan ly
    4. IR JAMMER   : Phat IR nhieu chan remote
    5. SETTINGS    : Sub-menu cai dat
    6. LG HACK     : (de trong - cho tinh nang tuong lai)

  SUB-MENU TV-B-GONE:
    0. BACK        : Quay lai menu chinh
    1. BAN NHANH   : Phat ma built-in + ma da luu
    2. WORLD CODES : Phat tat ca ma WORLD_IR_CODES (NA + EU)

  SUB-MENU SETTINGS:
    0. BACK        : Quay lai menu chinh
    1. CHON CHAN IR: Chon chan phat D5/D7 (luu flash)
    2. LED ON/OFF  : Bat/tat LED WS2812B (luu flash)
  ============================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include "app_types.h"
#include "codes_builtin.h"
#include "WORLD_IR_CODES.h"
#include "web_ui.h"

// ---------------------------- CAU HINH CHAN ---------------------------------
const uint16_t IR_TX_PIN_A = D5;   // GPIO14
const uint16_t IR_TX_PIN_B = D7;   // GPIO13
const uint16_t IR_RECV_PIN = D4;
const int BUTTON_PIN       = D3;

#define SDA_PIN D2
#define SCL_PIN D1

#define LED_PIN     D6
#define NUMPIXELS   1

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32

#define MAX_STATE_BYTES 64

// ---------------------------- DOI TUONG PHAN CUNG ---------------------------
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

IRsend irsendA(IR_TX_PIN_A);
IRsend irsendB(IR_TX_PIN_B);
IRsend *activeIrsend = &irsendA;

const uint16_t kCaptureBufferSize = 1024;
const uint8_t  kTimeout           = 50;
IRrecv irrecv(IR_RECV_PIN, kCaptureBufferSize, kTimeout, true);
decode_results results;

ESP8266WebServer server(80);

// ---------------------------- CONFIG (LUU TRONG FLASH) ----------------------
int currentTxPin   = 5;
int candidateTxPin = 5;
bool txPinConfirmed = false;
unsigned long txPinConfirmT = 0;
bool jammerFromWeb = false;
bool ledEnabled = true;  // Bat/tat LED WS2812B, luu flash

void loadConfig() {
  currentTxPin = 5;
  ledEnabled = true;
  if (LittleFS.exists("/config.json")) {
    File f = LittleFS.open("/config.json", "r");
    DynamicJsonDocument d(256);
    if (!deserializeJson(d, f)) {
      int p = d["txPin"] | 5;
      if (p == 5 || p == 7) currentTxPin = p;
      if (d.containsKey("ledEnabled")) ledEnabled = d["ledEnabled"].as<bool>();
    }
    f.close();
  }
  candidateTxPin = currentTxPin;
}

void saveConfig() {
  DynamicJsonDocument d(256);
  d["txPin"] = currentTxPin;
  d["ledEnabled"] = ledEnabled;
  File f = LittleFS.open("/config.json", "w");
  serializeJson(d, f);
  f.close();
}

void applyTxPin(int pin) {
  if (pin == 5) {
    activeIrsend = &irsendA;
    pinMode(IR_TX_PIN_B, OUTPUT);
    digitalWrite(IR_TX_PIN_B, LOW);
  } else {
    activeIrsend = &irsendB;
    pinMode(IR_TX_PIN_A, OUTPUT);
    digitalWrite(IR_TX_PIN_A, LOW);
  }
  currentTxPin = pin;
  candidateTxPin = pin;
}

// ---------------------------- QUAN LY MA IR DA LUU ---------------------------
DynamicJsonDocument savedDoc(24576);

bool loadSavedCodes() {
  savedDoc.clear();
  if (!LittleFS.exists("/codes.json")) { savedDoc.to<JsonArray>(); return true; }
  File f = LittleFS.open("/codes.json", "r");
  DeserializationError err = deserializeJson(savedDoc, f);
  f.close();
  if (err) { savedDoc.clear(); savedDoc.to<JsonArray>(); return false; }
  if (!savedDoc.is<JsonArray>()) savedDoc.to<JsonArray>();
  return true;
}

void persistSavedCodes() {
  File f = LittleFS.open("/codes.json", "w");
  serializeJson(savedDoc, f);
  f.close();
}

int savedCodeCount() { return savedDoc.as<JsonArray>().size(); }

String bytesToHex(const uint8_t *data, size_t len) {
  static const char hexch[] = "0123456789ABCDEF";
  String s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s += hexch[(data[i] >> 4) & 0xF];
    s += hexch[data[i] & 0xF];
  }
  return s;
}

size_t hexToBytes(const String &hex, uint8_t *out, size_t maxLen) {
  size_t n = hex.length() / 2;
  if (n > maxLen) n = maxLen;
  for (size_t i = 0; i < n; i++) {
    char buf[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
    out[i] = (uint8_t)strtoul(buf, nullptr, 16);
  }
  return n;
}

void sendSavedCode(int idx) {
  JsonArray arr = savedDoc.as<JsonArray>();
  if (idx < 0 || idx >= (int)arr.size()) return;
  JsonObject o = arr[idx];
  String type = o["type"].as<String>();

  if (type == "RAW") {
    uint16_t freq = o["freq"] | 38;
    JsonArray raw = o["raw"];
    uint16_t len = raw.size();
    if (len == 0) return;
    uint16_t *buf = new uint16_t[len];
    for (uint16_t k = 0; k < len; k++) buf[k] = raw[k].as<uint16_t>();
    activeIrsend->sendRaw(buf, len, freq);
    delete[] buf;
  } else if (o.containsKey("state")) {
    String hexStr = o["state"].as<String>();
    uint8_t buf[MAX_STATE_BYTES];
    size_t n = hexToBytes(hexStr, buf, MAX_STATE_BYTES);
    decode_type_t t = strToDecodeType(type.c_str());
    activeIrsend->send(t, buf, n);
  } else {
    decode_type_t t = strToDecodeType(type.c_str());
    const char *valStr = o["value"] | "0x0";
    uint64_t val = strtoull(valStr, nullptr, 16);
    uint16_t bits = o["bits"] | 32;
    activeIrsend->send(t, val, bits);
  }
}

void sendBuiltinCode(int idx) {
  if (idx < 0 || idx >= (int)BUILTIN_CODES_COUNT) return;
  const TvCode &c = BUILTIN_CODES[idx];
  activeIrsend->send(c.protocol, c.value, c.bits);
}

// Phat 1 ma World IR Code
// Format TV-B-Gone goc:
//   - moi index nen (bitcompression bit) dai dien cho 1 PAIR = mark + space
//   - times[] luu don vi 10us, khong phai microsecond
//   - codes[] duoc doc tu bit cao xuong bit thap (MSB-first)
// IRremoteESP8266 nhan timing truc tiep theo microsecond, nen phai * 10
// truoc khi phat.

static bool readWorldCompressedIndex(const IrCode *code,
                                     uint16_t *bitPos,
                                     uint8_t *outIndex) {
  if (!code || !bitPos || !outIndex) return false;

  const uint8_t bitsPerIndex = code->bitcompression;
  if (bitsPerIndex == 0 || bitsPerIndex > 4) return false;

  const uint32_t totalBits = (uint32_t)code->numpairs * bitsPerIndex;
  if ((uint32_t)(*bitPos) + bitsPerIndex > totalBits) return false;

  uint8_t value = 0;
  for (uint8_t i = 0; i < bitsPerIndex; i++) {
    const uint16_t byteIndex = (*bitPos) >> 3;
    const uint8_t bitInByte = (*bitPos) & 0x07;
    value = (uint8_t)((value << 1) |
                      ((code->codes[byteIndex] >> (7 - bitInByte)) & 0x01));
    (*bitPos)++;
  }

  *outIndex = value;
  return true;
}

static void sendWorldMarkUs(uint32_t usec) {
  // IRremoteESP8266 IRsend::mark() nhan uint16_t.
  // Du phong tach mark dai thanh nhieu doan neu database co gia tri > 65535us.
  while (usec > 65535UL) {
    activeIrsend->mark(65535);
    usec -= 65535UL;
  }
  if (usec > 0) activeIrsend->mark((uint16_t)usec);
}

static void sendWorldSpaceUs(uint32_t usec) {
  // IRsend::space() nhan uint32_t, nen giu nguyen cac khoang SPACE dai.
  if (usec > 0) activeIrsend->space(usec);
}

// ======================== IR JAMMER =========================================
// Cau hinh jammer - CHINH O DAY de giam duty / nhiet / sut ap.
// Duty tong = (mark/(mark+space)) * (burst/(burst+idle))
// Mac dinh: (200/1350) * (25/45) ~= 8%  (truoc day ~60%)
// ===== CONFIG IR JAMMER BÁC BỎ AGC & ĐÈ BẸP MỌI PROTOCOL =====
const uint16_t JAMMER_MARK_MIN_US  = 400;   // Ngưỡng tối thiểu để mạch lọc Bandpass mắt nhận sập
const uint16_t JAMMER_MARK_MAX_US  = 750;   // Độ dài vừa đủ đè bit remote, không bị AGC dập
const uint16_t JAMMER_SPACE_MIN_US = 250;   // Khoảng nghỉ cực ngắn để lấp đầy phổ sóng
const uint16_t JAMMER_SPACE_MAX_US = 450;   // Đảm bảo không tạo "khe hở" cho remote chen vào
const uint16_t JAMMER_BURST_MS     = 60;    // Phủ trọn khung truyền dữ liệu của remote (NEC ~67ms)
const uint16_t JAMMER_IDLE_MS      = 2;     // Chỉ nghỉ 2ms (vừa đủ nhả AGC, không cho remote thoát)

static void jammerPulseOnce(uint16_t durationMs) {
  if (durationMs == 0) return;
  activeIrsend->enableIROut(38);
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < durationMs) {
    uint16_t markUsec  = JAMMER_MARK_MIN_US
                       + (uint16_t)random(JAMMER_MARK_MAX_US  - JAMMER_MARK_MIN_US  + 1);
    uint16_t spaceUsec = JAMMER_SPACE_MIN_US
                       + (uint16_t)random(JAMMER_SPACE_MAX_US - JAMMER_SPACE_MIN_US + 1);
    activeIrsend->mark(markUsec);
    activeIrsend->space(spaceUsec);
  }
}

// Goi tu loop() moi vong - tu dong phat burst roi nghi, khong block lau.
void jammerStep() {
  static uint32_t lastChange = 0;
  static bool inBurst = false;

  uint32_t now = millis();
  if (!inBurst) {
    if (now - lastChange >= JAMMER_IDLE_MS) {
      inBurst = true;
      jammerPulseOnce(JAMMER_BURST_MS);   // block ~25ms
      lastChange = millis();
      inBurst = false;
    }
  }
}

// LED nhap nhay do khi jammer chay
void ledJammerPulse() {
  if (!ledEnabled) return;
  static uint32_t lastT = 0;
  static bool on = false;
  uint32_t now = millis();
  if (now - lastT < 90) return;
  lastT = now;
  on = !on;
  pixels.setPixelColor(0, on ? pixels.Color(255, 0, 0) : 0);
  pixels.show();
}

void sendWorldCode(const IrCode *code) {
  if (!code || code->numpairs == 0 || code->bitcompression == 0) return;

  const uint16_t freq = code->freq_kHz ? code->freq_kHz : 38;
  const uint8_t bitsPerIndex = code->bitcompression;
  const uint8_t maxTimeIndex = (uint8_t)(1U << bitsPerIndex);

  // Tuong duong sendRaw(), nhung phat truc tiep de:
  //   1) doc dung 1 index cho moi PAIR
  //   2) doi times[] tu 10us -> us
  //   3) khong bi gioi han uint16_t cua sendRaw() khi SPACE rat dai
  activeIrsend->enableIROut(freq);

  uint16_t bitPos = 0;

  for (uint8_t pair = 0; pair < code->numpairs; pair++) {
    uint8_t timeIndex = 0;
    if (!readWorldCompressedIndex(code, &bitPos, &timeIndex)) return;

    if (timeIndex >= maxTimeIndex) return;

    const uint16_t mark10us = code->times[(uint16_t)timeIndex * 2U];
    const uint16_t space10us = code->times[(uint16_t)timeIndex * 2U + 1U];

    // Database TV-B-Gone luu don vi 10 microsecond.
    const uint32_t markUs = (uint32_t)mark10us * 10UL;
    const uint32_t spaceUs = (uint32_t)space10us * 10UL;

    sendWorldMarkUs(markUs);
    sendWorldSpaceUs(spaceUs);
  }
}

// ======================== HIEU UNG LED WS2812B ==============================
uint32_t wheel(byte pos) {
  pos = 255 - pos;
  if (pos < 85)  return pixels.Color(255 - pos * 3, 0, pos * 3);
  if (pos < 170) { pos -= 85;  return pixels.Color(0, pos * 3, 255 - pos * 3); }
  pos -= 170;      return pixels.Color(pos * 3, 255 - pos * 3, 0);
}

void ledSetColor(uint32_t c) {
  if (!ledEnabled) return;
  pixels.setPixelColor(0, c); pixels.show();
}
void ledOff() { pixels.setPixelColor(0, 0); pixels.show(); }

void ledBreathe(byte hue) {
  if (!ledEnabled) return;
  static uint32_t lastT = 0;
  static float phase = 0;
  uint32_t now = millis();
  if (now - lastT < 15) return;
  lastT = now;
  phase += 0.06;
  float b = (sin(phase) + 1.0) / 2.0;
  uint8_t bright = 20 + (uint8_t)(b * 180);
  uint32_t base = wheel(hue);
  uint8_t r = (uint8_t)(((base >> 16) & 0xFF) * bright / 255);
  uint8_t g = (uint8_t)(((base >> 8) & 0xFF) * bright / 255);
  uint8_t b8 = (uint8_t)((base & 0xFF) * bright / 255);
  pixels.setPixelColor(0, pixels.Color(r, g, b8));
  pixels.show();
}

void ledRainbowSpin() {
  if (!ledEnabled) return;
  static uint32_t lastT = 0;
  static byte hue = 0;
  uint32_t now = millis();
  if (now - lastT < 12) return;
  lastT = now;
  hue += 3;
  ledSetColor(wheel(hue));
}

void ledFlash(uint32_t color, uint16_t ms) {
  if (!ledEnabled) { delay(ms); return; }
  ledSetColor(color);
  delay(ms);
  ledOff();
}

// ======================== TRANG THAI UNG DUNG ================================
AppMode mode = MENU;

// ---- Menu chinh (7 muc) ----
int menuIndex = 0;
const int MENU_COUNT = 7;
const char* MENU_NAMES[7] = {"TV-B-GONE", "PHAT IR LUU", "LUU IR MOI", "WIFI WEB", "IR JAMMER", "SETTINGS", "LG HACK"};
const byte  MENU_HUES[7]  = {0, 160, 90, 200, 0, 40, 120};

// ---- Sub-menu TV-B-Gone (0=BACK, 1=BAN NHANH, 2=WORLD CODES) ----
int tvbgoneSubIndex = 0;
int tvbgoneSubIndexBackup = 0;   // FIX: nho vi tri submenu truoc khi blast
const int TVBGONE_SUB_COUNT = 3;
const char* TVBGONE_SUB_NAMES[3] = {"< BACK", "BAN NHANH", "WORLD CODES"};

// ---- Sub-menu Settings (0=BACK, 1=CHON CHAN IR, 2=LED WS2812B) ----
int settingsSubIndex = 0;
const int SETTINGS_SUB_COUNT = 3;
const char* SETTINGS_SUB_NAMES[3] = {"< BACK", "CHAN IR", "LED ON/OFF"};

// ---- Trang thai blast ----
int blastIndex = 0;
int blastTotal = 0;
unsigned long lastBlastT = 0;
const unsigned long BLAST_GAP_MS = 25;
bool blastDone = false;
unsigned long blastDoneT = 0;
bool blastWorldMode = false;  // true = WORLD_IR_CODES mode
bool blastFromWeb = false;     // true = blast duoc khoi dong tu web, khong doi menu OLED

// Tong so world codes
int worldNACount = 0;
int worldEUCount = 0;
int worldTotal = 0;

// ---- Trang thai Playback ----
int playIndex = 0;
bool justSent = false;
unsigned long sentAtT = 0;

// ---- Trang thai Learn ----
bool waitingForCode = true;
bool haveTentative = false;
struct PendingCode {
  enum Kind { SIMPLE, STATE, RAW } kind = SIMPLE;
  decode_type_t protocol = decode_type_t::UNKNOWN;
  uint64_t value = 0;
  uint16_t bits = 0;
  uint8_t  stateBytes[MAX_STATE_BYTES];
  uint16_t stateLen = 0;
  uint16_t rawLen = 0;
  uint16_t *rawData = nullptr;
  uint16_t freq = 38;
} pending;

void freePending() {
  if (pending.kind == PendingCode::RAW && pending.rawData) delete[] pending.rawData;
  pending.rawData = nullptr;
  pending.kind = PendingCode::SIMPLE;
  haveTentative = false;
}

// ---- Trang thai WiFi ----
const char* AP_SSID = "TV-Killer";
const char* AP_PASS = "12345678";
String apIP = "";

// ======================== NUT BAM AUTO-SCROLL ================================
const unsigned long LONG_MS     = 600;   // Giu > 0.6s = LONG
const unsigned long VLONG_MS    = 3000;  // Giu > 3s = VLONG
const unsigned long AUTO_SCROLL_FIRST_MS = 600;   // Thoi gian giu de bat dau scroll
const unsigned long AUTO_SCROLL_REPEAT_MS = 300;  // Khoang cach giua cac lan scroll

ButtonEvent pollButton() {
  static bool down = false;
  static unsigned long downT = 0;
  static bool longFired = false;
  static unsigned long lastAutoT = 0;

  bool isDown = (digitalRead(BUTTON_PIN) == LOW);

  if (isDown && !down) {
    down = true;
    downT = millis();
    longFired = false;
    lastAutoT = 0;
    return EVT_NONE;
  }

  if (isDown && down) {
    unsigned long held = millis() - downT;
    if (held >= VLONG_MS && !longFired) {
      // VLONG se xu ly khi nha ra
    }
    // Auto-scroll: sau khi giu du LONG, phat EVT_LONG lien tuc
    if (held >= AUTO_SCROLL_FIRST_MS) {
      if (!longFired) {
        longFired = true;
        lastAutoT = millis();
        return EVT_LONG;
      } else {
        if (millis() - lastAutoT >= AUTO_SCROLL_REPEAT_MS) {
          lastAutoT = millis();
          return EVT_LONG;
        }
      }
    }
    return EVT_NONE;
  }

  if (!isDown && down) {
    down = false;
    unsigned long held = millis() - downT;
    delay(20);

    if (held >= VLONG_MS) return EVT_VLONG;
    if (longFired) return EVT_NONE;  // Da xu ly qua auto-scroll
    if (held >= LONG_MS) return EVT_LONG;
    return EVT_SHORT;
  }

  return EVT_NONE;
}

// ======================== OLED DISPLAY =======================================
void oledHeader(const String &title) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(title);
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
}

void oledScreen(const String &title, const String &line1, const String &line2 = "") {
  oledHeader(title);
  display.setCursor(0, 12);
  display.println(line1);
  display.setCursor(0, 22);
  display.println(line2);
  display.display();
}

// Ve dot chi vi tri trong danh sach
void drawDots(int cur, int total, int maxDots = 8) {
  if (total <= 1) return;
  int show = min(total, maxDots);
  int startX = (SCREEN_WIDTH - show * 7) / 2;
  // Tinh offset neu total > maxDots
  int offset = 0;
  if (total > maxDots && cur >= maxDots - 2) {
    offset = cur - (maxDots - 2);
    if (offset > total - maxDots) offset = total - maxDots;
  }
  for (int i = 0; i < show; i++) {
    int idx = i + offset;
    int cx = startX + i * 7;
    if (idx == cur) display.fillCircle(cx, 30, 2, SSD1306_WHITE);
    else display.drawCircle(cx, 30, 2, SSD1306_WHITE);
  }
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("MENU ");
  display.print(menuIndex + 1);
  display.print("/");
  display.print(MENU_COUNT);

  display.setCursor(0, 12);
  display.setTextSize(1);
  display.println(MENU_NAMES[menuIndex]);

  // Hien thi ten muc truoc / sau de de navigate
  if (menuIndex > 0) {
    display.setCursor(0, 22);
    display.print((char)0x11); // left arrow char
    display.print(" ");
    display.print(MENU_NAMES[menuIndex - 1]);
  }

  drawDots(menuIndex, MENU_COUNT);
  display.display();
}

// Sub-menu chung (co BACK o vi tri 0)
void drawSubMenu(const char* title, const char** names, int count, int idx) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(title);
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
  display.setCursor(0, 12);
  display.print("> ");
  display.println(names[idx]);
  display.setCursor(0, 22);
  display.print("Ngan=Chon  Giu=Sang");
  drawDots(idx, count);
  display.display();
}

void drawTvBGoneSubMenu() {
  drawSubMenu("TV-B-GONE", TVBGONE_SUB_NAMES, TVBGONE_SUB_COUNT, tvbgoneSubIndex);
}

void drawSettingsSubMenu() {
  drawSubMenu("SETTINGS", SETTINGS_SUB_NAMES, SETTINGS_SUB_COUNT, settingsSubIndex);
}

void drawBlastProgress() {
  display.clearDisplay();
  oledHeader(blastWorldMode ? "WORLD IR DANG BAN" : "TV-B-GONE BAN NHANH");
  display.setCursor(0, 12);
  if (!blastWorldMode) {
    if (blastIndex < (int)BUILTIN_CODES_COUNT) {
      display.print("Hang: ");
      display.println(BUILTIN_CODES[blastIndex].brand);
    } else {
      display.println("Ma da luu cua ban");
    }
  } else {
    if (blastIndex < worldNACount) {
      display.print("NA #");
      display.println(blastIndex);
    } else {
      display.print("EU #");
      display.println(blastIndex - worldNACount);
    }
  }
  int barW = SCREEN_WIDTH - 4;
  int fillW = blastTotal > 0 ? (int)((long)barW * blastIndex / blastTotal) : 0;
  display.drawRect(2, 24, barW, 7, SSD1306_WHITE);
  display.fillRect(2, 24, fillW, 7, SSD1306_WHITE);
  display.display();
}

void drawBlastDone() {
  oledScreen("HOAN TAT!", String(blastTotal) + " ma da ban.", "Ngan/Giu de ve menu");
}

void drawPlaybackEmpty() {
  oledScreen("PHAT IR DA LUU", "Chua co ma nao.", "Giu that lau de thoat");
}

void drawPlaybackList() {
  int total = savedCodeCount();
  if (total == 0) { drawPlaybackEmpty(); return; }
  JsonObject o = savedDoc.as<JsonArray>()[playIndex];
  String line1 = String("> ") + o["name"].as<const char*>();
  String kind = o.containsKey("state") ? "AC/STATE" : o["type"].as<String>();
  String line2 = kind + " (" + String(playIndex + 1) + "/" + String(total) + ") Ngan=Phat";
  oledScreen("PHAT IR DA LUU", line1, line2);
}

void drawPlaybackSent() {
  oledScreen("DA GUI IR!", "", "");
}

void drawLearnWaiting() {
  static const char spin[4] = {'|', '/', '-', '\\'};
  static uint32_t lastT = 0;
  static int f = 0;
  if (millis() - lastT > 150) { lastT = millis(); f = (f + 1) % 4; }
  String l1 = String("Dang cho... ") + spin[f];
  String l2 = "Da luu: " + String(savedCodeCount()) + " ma";
  oledScreen("LUU IR MOI", l1, l2);
}

void drawLearnResult() {
  String title, line1;
  if (pending.kind == PendingCode::RAW) {
    title = "RAW";
    line1 = String(pending.rawLen) + " xung";
  } else if (pending.kind == PendingCode::STATE) {
    title = typeToString(pending.protocol);
    line1 = String("AC - ") + pending.stateLen + " byte";
  } else {
    title = typeToString(pending.protocol);
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%08lX %db", (unsigned long)(pending.value & 0xFFFFFFFF), pending.bits);
    line1 = String(buf);
  }
  oledScreen(title, line1, "Ngan=LUU   Giu=Bo qua");
}

void drawLearnSaved(const String &name) {
  oledScreen("DA LUU!", name, "");
}

void drawWifiScreen() {
  oledScreen(String("WIFI: ") + AP_SSID, String("Pass: ") + AP_PASS, "IP: " + apIP);
}

void drawTxPinMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("CHON CHAN IR");
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);

  display.setCursor(0, 12);
  display.print(candidateTxPin == 5 ? "> D5" : "  D5");
  if (currentTxPin == 5) display.print(" [dang dung]");

  display.setCursor(0, 22);
  display.print(candidateTxPin == 7 ? "> D7" : "  D7");
  if (currentTxPin == 7) display.print(" [dang dung]");

  display.display();
}

void drawTxPinConfirmed() {
  oledScreen("DA AP DUNG!", String("Phat qua D") + currentTxPin, "Chan con lai=LOW");
}

void drawLedMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("LED WS2812B");
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
  display.setCursor(0, 12);
  display.print("Trang thai: ");
  display.println(ledEnabled ? "BAT" : "TAT");
  display.setCursor(0, 22);
  display.println("Ngan=Doi  Giu=Luu+Ve");
  display.display();
}

void drawLgHack() {
  oledScreen("LG HACK", "(De trong)", "Tinh nang sap co");
}

void drawIrJammer() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("IR JAMMER");
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
  display.setCursor(0, 12);
  display.println("Dang phat IR nhieu...");
  display.setCursor(0, 22);
  display.println("Ngan/Giu = THOAT");
  display.display();
}

// ======================== CHUYEN CHE DO =====================================
void enterMenu() {
  mode = MENU;
  freePending();
  ledOff();
  drawMenu();
}

// FIX #1: nhan tham so idx de giu vi tri submenu
void enterTvBGoneSubMenu(int idx) {
  mode = TVBGONE_SUBMENU;
  tvbgoneSubIndex = idx;
  drawTvBGoneSubMenu();
}

void startBlast(bool worldMode, bool fromWeb = false) {
  // FIX #2: luu vi tri submenu truoc khi blast (chi khi bat tu OLED)
  if (!fromWeb) tvbgoneSubIndexBackup = tvbgoneSubIndex;

  blastWorldMode = worldMode;
  blastFromWeb = fromWeb;
  loadSavedCodes();
  blastIndex = 0;
  blastDone = false;
  lastBlastT = millis() - BLAST_GAP_MS;

  if (worldMode) {
    worldNACount = (int)num_NAcodes;
    worldEUCount = (int)num_EUcodes;
    worldTotal   = worldNACount + worldEUCount;
    blastTotal   = worldTotal;
    if (!fromWeb) mode = MODE_TVBGONE_WORLD;
  } else {
    blastTotal = BUILTIN_CODES_COUNT + savedCodeCount();
    if (!fromWeb) mode = MODE_TVBGONE_SAVED;
  }

  // Khi bat tu web, KHONG doi mode. MODE_WIFI phai duoc giu nguyen
  // de loop() tiep tuc goi server.handleClient().
  if (!fromWeb) drawBlastProgress();
}

// Chay 1 buoc cua TV-B-GONE. Ham nay duoc dung ca tu OLED va tu Web.
// Tra ve true neu blast dang ton tai/duoc xu ly, false neu khong co blast.
bool serviceBlastStep() {
  if (blastTotal <= 0) {
    blastDone = true;
    return false;
  }

  if (blastDone) {
    return true;
  }

  if (millis() - lastBlastT < BLAST_GAP_MS) return true;

  lastBlastT = millis();

  if (blastIndex < blastTotal) {
    if (blastWorldMode) {
      if (blastIndex < worldNACount) {
        sendWorldCode(NApowerCodes[blastIndex]);
      } else {
        sendWorldCode(EUpowerCodes[blastIndex - worldNACount]);
      }
    } else {
      if (blastIndex < (int)BUILTIN_CODES_COUNT) {
        sendBuiltinCode(blastIndex);
      } else {
        sendSavedCode(blastIndex - BUILTIN_CODES_COUNT);
      }
    }

    ledSetColor(wheel((blastIndex * (blastWorldMode ? 7 : 11)) % 255));

    // OLED rat cham (~15-25ms/lan ve). Chi ve moi 5 ma de tang toc.
    static uint16_t lastDrawnIdx = 0xFFFF;
    if (!blastFromWeb) {
      if (blastIndex == 0 || blastIndex - lastDrawnIdx >= 5 || blastIndex + 1 >= blastTotal) {
        drawBlastProgress();
        lastDrawnIdx = blastIndex;
      }
    }
    blastIndex++;
  }

  if (blastIndex >= blastTotal) {
    blastDone = true;
    blastDoneT = millis();
    ledFlash(pixels.Color(0, 255, 0), 120);

    if (!blastFromWeb) {
      drawBlastDone();
    }
  }

  return true;
}

void enterPlayback() {
  mode = MODE_PLAYBACK;
  loadSavedCodes();
  playIndex = 0;
  justSent = false;
  drawPlaybackList();
}

void enterLearn() {
  mode = MODE_LEARN;
  loadSavedCodes();
  waitingForCode = true;
  freePending();
  irrecv.enableIRIn();
  drawLearnWaiting();
}

void startWifiAP();

void enterWifi() {
  mode = MODE_WIFI;
  loadSavedCodes();
  startWifiAP();
  drawWifiScreen();
}

void exitWifi() {
  jammerFromWeb = false;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void enterSettingsSubMenu() {
  mode = SETTINGS_SUBMENU;
  settingsSubIndex = 0;
  drawSettingsSubMenu();
}

void enterTxPinMenu() {
  mode = MODE_TXPIN;
  candidateTxPin = currentTxPin;
  txPinConfirmed = false;
  drawTxPinMenu();
}

void enterLedMenu() {
  mode = MODE_LED_CONTROL;
  drawLedMenu();
}

void enterLgHack() {
  mode = MODE_LGHACK;
  drawLgHack();
}

void enterIrJammer() {
  mode = MODE_IR_JAMMER;
  jammerFromWeb = false;
  drawIrJammer();
}

// ======================== WEB SERVER =========================================
void handleApiStatus() {
  DynamicJsonDocument doc(512);
  doc["mode"] = (int)mode;
  doc["blastIndex"] = blastIndex;
  doc["blastTotal"] = blastTotal;
  doc["savedCount"] = savedCodeCount();
  doc["jamming"] = jammerFromWeb;
  doc["txPin"] = currentTxPin;
  doc["ledEnabled"] = ledEnabled;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiCodes() {
  String out;
  serializeJson(savedDoc, out);
  server.send(200, "application/json", out);
}

void handleApiSend() {
  if (!server.hasArg("index")) { server.send(400, "text/plain", "thieu index"); return; }
  sendSavedCode(server.arg("index").toInt());
  server.send(200, "text/plain", "OK");
}

void handleApiSendBuiltin() {
  if (!server.hasArg("index")) { server.send(400, "text/plain", "thieu index"); return; }
  sendBuiltinCode(server.arg("index").toInt());
  server.send(200, "text/plain", "OK");
}

void handleApiDelete() {
  if (!server.hasArg("index")) { server.send(400, "text/plain", "thieu index"); return; }
  int idx = server.arg("index").toInt();
  JsonArray arr = savedDoc.as<JsonArray>();
  if (idx >= 0 && idx < (int)arr.size()) {
    arr.remove(idx);
    persistSavedCodes();
  }
  server.send(200, "text/plain", "OK");
}

void handleApiAdd() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "thieu body"); return; }
  DynamicJsonDocument in(8192);
  DeserializationError err = deserializeJson(in, server.arg("plain"));
  if (err) { server.send(400, "text/plain", "JSON loi"); return; }

  JsonArray arr = savedDoc.as<JsonArray>();
  JsonObject o = arr.createNestedObject();
  String name = in["name"] | ("ir" + String(arr.size()));
  o["name"] = name;
  String type = in["type"] | "RAW";
  o["type"] = type;

  if (type == "RAW") {
    o["freq"] = in["freq"] | 38;
    JsonArray src = in["raw"];
    JsonArray dst = o.createNestedArray("raw");
    for (JsonVariant v : src) dst.add(v.as<uint16_t>());
  } else if (in.containsKey("state")) {
    o["state"] = in["state"].as<String>();
    o["bytes"] = in["bytes"] | (int)(in["state"].as<String>().length() / 2);
  } else {
    o["value"] = in["value"] | "0x0";
    o["bits"] = in["bits"] | 32;
  }
  persistSavedCodes();
  server.send(200, "text/plain", "OK");
}

// API: Thu IR bang web (thay vi nhap hex tay)
void handleApiTestIr() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "thieu body"); return; }
  DynamicJsonDocument in(4096);
  DeserializationError err = deserializeJson(in, server.arg("plain"));
  if (err) { server.send(400, "text/plain", "JSON loi"); return; }

  String type = in["type"] | "NEC";
  if (type == "RAW") {
    uint16_t freq = in["freq"] | 38;
    JsonArray raw = in["raw"];
    uint16_t len = raw.size();
    if (len == 0) { server.send(400, "text/plain", "raw rong"); return; }
    uint16_t *buf = new uint16_t[len];
    for (uint16_t k = 0; k < len; k++) buf[k] = raw[k].as<uint16_t>();
    activeIrsend->sendRaw(buf, len, freq);
    delete[] buf;
  } else {
    decode_type_t t = strToDecodeType(type.c_str());
    const char *valStr = in["value"] | "0x0";
    uint64_t val = strtoull(valStr, nullptr, 16);
    uint16_t bits = in["bits"] | 32;
    activeIrsend->send(t, val, bits);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiJammer() {
  if (!server.hasArg("state")) {
    server.send(400, "text/plain", "thieu state");
    return;
  }
  String st = server.arg("state");
  jammerFromWeb = (st == "1" || st == "on" || st == "true");
  if (!jammerFromWeb && ledEnabled) ledOff();
  server.send(200, "text/plain", jammerFromWeb ? "JAMMING" : "STOPPED");
}

void handleApiTvbgoneStart() {
  String modeStr = server.arg("mode");
  bool worldMode = (modeStr == "world");

  // QUAN TRONG: khoi dong tu Web phai giu MODE_WIFI.
  // Neu startBlast() doi sang MODE_TVBGONE_* thi loop() se khong
  // goi server.handleClient() nua va trang web se treo.
  startBlast(worldMode, true);
  server.send(200, "text/plain", "OK");
}

void handleApiBuiltinList() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < BUILTIN_CODES_COUNT; i++) {
    JsonObject o = arr.createNestedObject();
    o["brand"] = BUILTIN_CODES[i].brand;
    o["protocol"] = typeToString(BUILTIN_CODES[i].protocol);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiTxPin() {
  if (!server.hasArg("pin")) { server.send(400, "text/plain", "thieu pin"); return; }
  int p = server.arg("pin").toInt();
  if (p != 5 && p != 7) { server.send(400, "text/plain", "pin phai la 5 hoac 7"); return; }
  applyTxPin(p);
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void handleApiLed() {
  if (!server.hasArg("state")) { server.send(400, "text/plain", "thieu state"); return; }
  String st = server.arg("state");
  ledEnabled = (st == "1" || st == "on" || st == "true");
  if (!ledEnabled) ledOff();
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void startWifiAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  apIP = WiFi.softAPIP().toString();

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/api/status",           HTTP_GET,  handleApiStatus);
  server.on("/api/codes",            HTTP_GET,  handleApiCodes);
  server.on("/api/builtin",          HTTP_GET,  handleApiBuiltinList);
  server.on("/api/send",             HTTP_POST, handleApiSend);
  server.on("/api/send_builtin",     HTTP_POST, handleApiSendBuiltin);
  server.on("/api/delete",           HTTP_POST, handleApiDelete);
  server.on("/api/add",              HTTP_POST, handleApiAdd);
  server.on("/api/test_ir",          HTTP_POST, handleApiTestIr);
  server.on("/api/tvbgone/start",    HTTP_POST, handleApiTvbgoneStart);
  server.on("/api/txpin",            HTTP_POST, handleApiTxPin);
  server.on("/api/led",              HTTP_POST, handleApiLed);
  server.on("/api/jammer",           HTTP_POST, handleApiJammer);
  server.begin();
}

// ======================== SETUP ==============================================
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  randomSeed(micros());
  pixels.begin();
  pixels.clear();
  pixels.show();

  irsendA.begin();
  irsendB.begin();
  irrecv.enableIRIn();

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Khong tim thay OLED SSD1306!"));
    for (;;) delay(1000);
  }

  if (!LittleFS.begin()) {
    Serial.println(F("Loi LittleFS, format..."));
    LittleFS.format();
    LittleFS.begin();
  }
  loadSavedCodes();
  loadConfig();
  applyTxPin(currentTxPin);

  enterMenu();
  Serial.println(F("TV Killer Pro v3 - San sang!"));
}

// ======================== LOOP ===============================================
void loop() {
  ButtonEvent evt = pollButton();

  // Giu that lau (>3s) o bat ky che do nao tru MENU => ve menu chinh
  if (evt == EVT_VLONG && mode != MENU) {
    if (mode == MODE_WIFI) exitWifi();
    if (mode == MODE_LEARN) irrecv.pause();
    enterMenu();
    return;
  }

  switch (mode) {

    // ============================================================== MENU CHINH
    case MENU: {
      ledBreathe(MENU_HUES[menuIndex]);
      if (evt == EVT_LONG) {
        menuIndex = (menuIndex + 1) % MENU_COUNT;
        drawMenu();
      } else if (evt == EVT_SHORT) {
          switch (menuIndex) {
            case 0: enterTvBGoneSubMenu(0); break;   // FIX #3: truyen 0 khi vao lan dau
            case 1: enterPlayback(); break;
            case 2: enterLearn(); break;
            case 3: enterWifi(); break;
            case 4: enterIrJammer(); break;
            case 5: enterSettingsSubMenu(); break;
            case 6: enterLgHack(); break;
          }
        }
      break;
    }

    // ======================================================== SUB-MENU TV-B-GONE
    case TVBGONE_SUBMENU: {
      ledBreathe(0);
      if (evt == EVT_LONG) {
        tvbgoneSubIndex = (tvbgoneSubIndex + 1) % TVBGONE_SUB_COUNT;
        drawTvBGoneSubMenu();
      } else if (evt == EVT_SHORT) {
        switch (tvbgoneSubIndex) {
          case 0: enterMenu(); break;          // BACK
          case 1: startBlast(false); break;    // BAN NHANH (builtin + saved)
          case 2: startBlast(true); break;     // WORLD CODES
        }
      }
      break;
    }

    // ================================================= TVBGONE - BAN NHANH
    case MODE_TVBGONE_SAVED: {
      // FIX #4a: quay ve submenu voi vi tri da luu
      if (evt == EVT_SHORT || evt == EVT_LONG) {
        enterTvBGoneSubMenu(tvbgoneSubIndexBackup);
        break;
      }
      serviceBlastStep();
      if (blastDone && millis() - blastDoneT > 1500)
        enterTvBGoneSubMenu(tvbgoneSubIndexBackup);
      break;
    }

    // ================================================= TVBGONE - WORLD CODES
    case MODE_TVBGONE_WORLD: {
      // FIX #4b: quay ve submenu voi vi tri da luu
      if (evt == EVT_SHORT || evt == EVT_LONG) {
        enterTvBGoneSubMenu(tvbgoneSubIndexBackup);
        break;
      }
      serviceBlastStep();
      if (blastDone && millis() - blastDoneT > 1500)
        enterTvBGoneSubMenu(tvbgoneSubIndexBackup);
      break;
    }

    // ============================================================== PLAYBACK
    case MODE_PLAYBACK: {
      int total = savedCodeCount();
      if (total == 0) { ledBreathe(160); break; }
      if (justSent) {
        ledBreathe(180);
        if (millis() - sentAtT > 700) { justSent = false; drawPlaybackList(); }
        break;
      }
      ledBreathe(160);
      if (evt == EVT_LONG) {
        playIndex = (playIndex + 1) % total;
        drawPlaybackList();
      } else if (evt == EVT_SHORT) {
        sendSavedCode(playIndex);
        ledFlash(pixels.Color(0, 200, 255), 150);
        justSent = true; sentAtT = millis();
        drawPlaybackSent();
      }
      break;
    }

    // ================================================================= LEARN
    case MODE_LEARN: {
      if (waitingForCode) {
        ledBreathe(140);
        drawLearnWaiting();
        if (irrecv.decode(&results)) {
          if (hasACState(results.decode_type)) {
            pending.kind = PendingCode::STATE;
            pending.protocol = results.decode_type;
            pending.stateLen = results.bits / 8;
            if (pending.stateLen > MAX_STATE_BYTES) pending.stateLen = MAX_STATE_BYTES;
            memcpy(pending.stateBytes, results.state, pending.stateLen);
          } else if (results.decode_type != decode_type_t::UNKNOWN) {
            pending.kind = PendingCode::SIMPLE;
            pending.protocol = results.decode_type;
            pending.value = results.value;
            pending.bits = results.bits;
          } else {
            pending.kind = PendingCode::RAW;
            pending.rawLen = getCorrectedRawLength(&results);
            pending.rawData = resultToRawArray(&results);
            pending.freq = 38;
          }
          irrecv.resume();
          haveTentative = true;
          waitingForCode = false;
          ledFlash(pixels.Color(255, 255, 255), 100);
          drawLearnResult();
        }
      } else if (haveTentative) {
        if (evt == EVT_LONG) {
          freePending();
          waitingForCode = true;
          ledFlash(pixels.Color(255, 60, 0), 120);
        } else if (evt == EVT_SHORT) {
          String name = "ir" + String(savedCodeCount() + 1);
          JsonArray arr = savedDoc.as<JsonArray>();
          JsonObject o = arr.createNestedObject();
          o["name"] = name;
          if (pending.kind == PendingCode::RAW) {
            o["type"] = "RAW";
            o["freq"] = pending.freq;
            JsonArray raw = o.createNestedArray("raw");
            for (uint16_t i = 0; i < pending.rawLen; i++) raw.add(pending.rawData[i]);
          } else if (pending.kind == PendingCode::STATE) {
            o["type"] = typeToString(pending.protocol);
            o["state"] = bytesToHex(pending.stateBytes, pending.stateLen);
            o["bytes"] = pending.stateLen;
          } else {
            o["type"] = typeToString(pending.protocol);
            char buf[24];
            snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)pending.value);
            o["value"] = buf;
            o["bits"] = pending.bits;
          }
          persistSavedCodes();
          freePending();
          ledFlash(pixels.Color(0, 255, 0), 150);
          drawLearnSaved(name);
          delay(700);
          waitingForCode = true;
        }
      }
      break;
    }

    // ================================================================== WIFI
    case MODE_WIFI: {
      ledRainbowSpin();

      // Phuc vu web lien tuc. Quan trong: blast khoi dong tu web
      // khong duoc doi mode, nen AP/server van song trong suot qua trinh.
      server.handleClient();

      // Neu co blast do web yeu cau, thuc hien 1 code roi quay lai day.
      if (blastFromWeb) {
        serviceBlastStep();
      }
      if (jammerFromWeb) {
        ledJammerPulse();
        jammerStep();
      }
      // Khi blast web xong, giu nguyen MODE_WIFI va AP, khong quay ve
      // menu TV-B-GONE. Chi clear co neu da hoan tat de lan sau start lai duoc.
      if (blastFromWeb && blastDone) {
        blastFromWeb = false;
      }

      if (evt == EVT_SHORT || evt == EVT_LONG) {
        exitWifi();
        enterMenu();
      }
      break;
    }

    // ======================================================= SUB-MENU SETTINGS
    case SETTINGS_SUBMENU: {
      ledBreathe(40);
      if (evt == EVT_LONG) {
        settingsSubIndex = (settingsSubIndex + 1) % SETTINGS_SUB_COUNT;
        drawSettingsSubMenu();
      } else if (evt == EVT_SHORT) {
        switch (settingsSubIndex) {
          case 0: enterMenu(); break;        // BACK
          case 1: enterTxPinMenu(); break;   // CHAN IR
          case 2: enterLedMenu(); break;     // LED ON/OFF
        }
      }
      break;
    }

    // ============================================================== CHAN IR
    // NGAN = ap dung.  GIU = doi D5/D7
    case MODE_TXPIN: {
      ledBreathe(40);
      if (txPinConfirmed) {
        if (millis() - txPinConfirmT > 1200) enterSettingsSubMenu();
        break;
      }
      if (evt == EVT_LONG) {
        candidateTxPin = (candidateTxPin == 5) ? 7 : 5;
        drawTxPinMenu();
      } else if (evt == EVT_SHORT) {
        applyTxPin(candidateTxPin);
        saveConfig();
        ledFlash(pixels.Color(0, 255, 0), 150);
        txPinConfirmed = true;
        txPinConfirmT = millis();
        drawTxPinConfirmed();
      }
      break;
    }

    // =========================================================== LED ON/OFF
    // NGAN = doi on/off (hien thi ngay).  GIU = luu va ve settings
    case MODE_LED_CONTROL: {
      if (evt == EVT_SHORT) {
        ledEnabled = !ledEnabled;
        if (!ledEnabled) ledOff();
        saveConfig();
        drawLedMenu();
      } else if (evt == EVT_LONG) {
        saveConfig();
        enterSettingsSubMenu();
      }
      if (ledEnabled) ledBreathe(90);
      break;
    }

    // ============================================================== IR JAMMER
    case MODE_IR_JAMMER: {
      ledJammerPulse();
      jammerStep();
      if (evt == EVT_SHORT || evt == EVT_LONG) {
        ledOff();
        enterMenu();
      }
      break;
    }

    // ============================================================== LG HACK
    case MODE_LGHACK: {
      ledBreathe(120);
      drawLgHack();
      if (evt == EVT_SHORT || evt == EVT_LONG) {
        enterMenu();
      }
      break;
    }
  }
}