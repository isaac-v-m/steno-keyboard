// Steno keyboard firmware for the Arduino Pro Micro (ATmega32U4).
//
// Scans the COL2ROW key matrix from Steno_Board.kicad_sch, collects every
// key pressed during a stroke, and when all keys are released sends one packet
// over USB serial for steno_interpreter.py:
//
//   0xFF, keys0, keys1, keys2, keys3, flags
//
// keys0 = L00-L06, keys1 = L07-L12, keys2 = R00-R06, keys3 = R07-R12, with
// bit n set for the nth key in each group. Only bits 0-6 are ever used, so no
// data byte can be mistaken for the 0xFF start byte.
// flags: bit0 = left layer 2, bit1 = thumb layer 2, bit2 = right layer 2.
//
// The last stroke is also shown on the 128x32 I2C OLED (SSD1306, address 0x3C).
// Board setting: "Arduino Leonardo" for a 5V/16MHz Pro Micro.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Matrix pins (see the note on the schematic). Each hand has its own rows and
// columns, wired only to its side of the Pro Micro: r1-r3/c1-c5 are the left
// hand, r4-r6/c6-c10 the right. D0/D1 are plain GPIO here; USB serial is
// separate from the TX/RX pins on the ATmega32U4.
const uint8_t ROW_PINS[] = {1, 0, 9, 21, 20, 10};                // r1-r6
const uint8_t COL_PINS[] = {4, 5, 6, 7, 8, 19, 18, 15, 14, 16};  // c1-c10
const uint8_t NUM_ROWS = sizeof(ROW_PINS);
const uint8_t NUM_COLS = sizeof(COL_PINS);

// Key at each matrix position; empty strings are unpopulated positions.
const char *const KEY_NAMES[NUM_ROWS][NUM_COLS] = {
  {"L00", "L01", "L02", "L03", "L04", "",    "",    "",    "",    ""},  // r1 left top
  {"L05", "L06", "L07", "L08", "L09", "",    "",    "",    "",    ""},  // r2 left bottom
  {"",    "",    "L10", "L11", "L12", "",    "",    "",    "",    ""},  // r3 left thumbs
  {"",    "",    "",    "",    "",    "R00", "R01", "R02", "R03", "R04"},  // r4 right top
  {"",    "",    "",    "",    "",    "R05", "R06", "R07", "R08", "R09"},  // r5 right bottom
  {"",    "",    "",    "",    "",    "R10", "R11", "R12", "",    ""},     // r6 right thumbs
};

const uint8_t START_BYTE = 0xFF;
const uint8_t KEY_BYTES = 4;
const uint8_t KEYS_PER_BYTE = 7;
const uint8_t LEFT_LAYER_BIT = 0;
const uint8_t THUMB_LAYER_BIT = 1;
const uint8_t RIGHT_LAYER_BIT = 2;

const uint8_t DEBOUNCE_MS = 5;
const uint8_t ROW_SETTLE_US = 10;

Adafruit_SSD1306 display(128, 32, &Wire, -1);
bool displayOk = false;

uint16_t rawRows[NUM_ROWS];       // last raw scan, bit c = column c+1
uint16_t stableRows[NUM_ROWS];    // debounced state
uint16_t strokeRows[NUM_ROWS];    // every key pressed during the current stroke
unsigned long lastChangeMs = 0;

// Layer flags sent with each stroke. Nothing sets these yet: there are no
// layer keys on the board, so every stroke is sent as layer 1.
uint8_t layerFlags = 0;

void scanMatrix(uint16_t rows[]) {
  for (uint8_t r = 0; r < NUM_ROWS; r++) {
    // Drive only this row LOW; the others stay high-impedance.
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(ROW_SETTLE_US);

    uint16_t bits = 0;
    for (uint8_t c = 0; c < NUM_COLS; c++) {
      if (digitalRead(COL_PINS[c]) == LOW) {
        bits |= 1 << c;
      }
    }
    rows[r] = bits;

    pinMode(ROW_PINS[r], INPUT);
  }
}

bool anyPressed(const uint16_t rows[]) {
  for (uint8_t r = 0; r < NUM_ROWS; r++) {
    if (rows[r]) return true;
  }
  return false;
}

void sendStroke(const uint16_t rows[]) {
  uint8_t packet[1 + KEY_BYTES + 1] = {START_BYTE};
  for (uint8_t r = 0; r < NUM_ROWS; r++) {
    for (uint8_t c = 0; c < NUM_COLS; c++) {
      const char *name = KEY_NAMES[r][c];
      if (!name[0] || !(rows[r] & (1 << c))) continue;
      // "L07" -> left group, key 7 -> byte 1 bit 0; "R" keys use bytes 2-3.
      uint8_t n = (name[1] - '0') * 10 + (name[2] - '0');
      uint8_t index = (name[0] == 'R' ? 2 : 0) + n / KEYS_PER_BYTE;
      packet[1 + index] |= 1 << (n % KEYS_PER_BYTE);
    }
  }
  packet[1 + KEY_BYTES] = layerFlags & 0x7F;
  Serial.write(packet, sizeof(packet));
}

void showStroke(const uint16_t rows[]) {
  if (!displayOk) return;
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(F("L"));
  display.print(layerFlags & (1 << LEFT_LAYER_BIT) ? 2 : 1);
  display.print(F(" T"));
  display.print(layerFlags & (1 << THUMB_LAYER_BIT) ? 2 : 1);
  display.print(F(" R"));
  display.println(layerFlags & (1 << RIGHT_LAYER_BIT) ? 2 : 1);
  for (uint8_t r = 0; r < NUM_ROWS; r++) {
    for (uint8_t c = 0; c < NUM_COLS; c++) {
      if (rows[r] & (1 << c)) {
        display.print(KEY_NAMES[r][c]);
        display.print(' ');
      }
    }
  }
  display.display();
}

void setup() {
  for (uint8_t r = 0; r < NUM_ROWS; r++) {
    pinMode(ROW_PINS[r], INPUT);
  }
  for (uint8_t c = 0; c < NUM_COLS; c++) {
    pinMode(COL_PINS[c], INPUT_PULLUP);
  }

  Serial.begin(9600);  // USB serial: the baud rate is ignored

  displayOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (displayOk) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("Steno ready"));
    display.display();
  }
}

void loop() {
  uint16_t rows[NUM_ROWS];
  scanMatrix(rows);

  unsigned long now = millis();
  if (memcmp(rows, rawRows, sizeof(rows)) != 0) {
    memcpy(rawRows, rows, sizeof(rows));
    lastChangeMs = now;
    return;
  }
  if (now - lastChangeMs < DEBOUNCE_MS || memcmp(rows, stableRows, sizeof(rows)) == 0) {
    return;
  }

  // The matrix has been stable long enough and differs from the last state.
  memcpy(stableRows, rows, sizeof(rows));
  if (anyPressed(stableRows)) {
    for (uint8_t r = 0; r < NUM_ROWS; r++) {
      strokeRows[r] |= stableRows[r];
    }
  } else if (anyPressed(strokeRows)) {
    // All keys released: the stroke is complete.
    sendStroke(strokeRows);
    showStroke(strokeRows);
    memset(strokeRows, 0, sizeof(strokeRows));
  }
}
