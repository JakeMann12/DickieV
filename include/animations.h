#pragma once

#include <Arduino.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>
#include "phrases.h"

enum {
  RUN_A,
  RUN_B,
  AIR,
  DUNK,
  BALL,
  HOOP,
  HOOP_SHAKE,
  SHOOT
};

inline void initDunkSprites(hd44780_pinIO& lcd) {
  byte runA[8] = {0b01110, 0b01110, 0b00100, 0b11111, 0b00100, 0b01010, 0b10010, 0b00000};
  byte runB[8] = {0b01110, 0b01110, 0b00100, 0b11111, 0b00100, 0b01010, 0b01001, 0b00000};
  byte air[8] = {0b10101, 0b01110, 0b01110, 0b00100, 0b00100, 0b01010, 0b01010, 0b00000};
  byte dunk[8] = {0b00111, 0b01110, 0b01111, 0b00110, 0b00100, 0b01010, 0b01010, 0b00000};
  byte ball[8] = {0b01110, 0b11111, 0b11111, 0b11111, 0b01110, 0b00000, 0b00000, 0b00000};
  byte hoop[8] = {0b00001, 0b00001, 0b00001, 0b11101, 0b00101, 0b00101, 0b00000, 0b00000};
  byte hoopS[8] = {0b00001, 0b00001, 0b00001, 0b01111, 0b00101, 0b01001, 0b00000, 0b00000};
  byte shoot[8] = {0b00011, 0b01110, 0b01110, 0b00110, 0b00100, 0b00100, 0b01010, 0b01010};

  lcd.createChar(RUN_A, runA);
  lcd.createChar(RUN_B, runB);
  lcd.createChar(AIR, air);
  lcd.createChar(DUNK, dunk);
  lcd.createChar(BALL, ball);
  lcd.createChar(HOOP, hoop);
  lcd.createChar(HOOP_SHAKE, hoopS);
  lcd.createChar(SHOOT, shoot);
}

inline void putSprite(hd44780_pinIO& lcd, uint8_t col, uint8_t row, uint8_t ch) {
  lcd.setCursor(col, row);
  lcd.write(ch);
}

inline void blankCell(hd44780_pinIO& lcd, uint8_t col, uint8_t row) {
  lcd.setCursor(col, row);
  lcd.print(' ');
}

inline void centeredText(hd44780_pinIO& lcd, uint8_t row, const char* s) {
  uint8_t len = strlen(s);
  lcd.setCursor(len >= 16 ? 0 : (16 - len) / 2, row);
  lcd.print(s);
}

inline void letterRevealAnimation(hd44780_pinIO& lcd, const char* word,
                                  uint16_t letterMs = 350) {
  String w(word);
  uint8_t len = w.length();
  uint8_t startCol = (len >= 16) ? 0 : (16 - len) / 2;

  lcd.clear();

  // Type it out: D, DU, DUK, DUKE
  for (uint8_t i = 1; i <= len; i++) {
    lcd.setCursor(startCol, 0);
    lcd.print(w.substring(0, i));
    delay(letterMs);
  }
  delay(500);

  // Blink the full word 3 times
  for (uint8_t i = 0; i < 3; i++) {
    lcd.clear();
    delay(150);
    lcd.setCursor(startCol, 0);
    lcd.print(w);
    delay(250);
  }
}

// Cycles DUKE -> DDMF -> GTHC each time it's called
inline void preGameHype(hd44780_pinIO& lcd) {
  static const char* const WORDS[] = {"DUKE", "DDMF", "GTHC"};
  static uint8_t idx = 0;
  letterRevealAnimation(lcd, WORDS[idx]);
  idx = (idx + 1) % 3;
}

inline void scrollTextLine(hd44780_pinIO& lcd, uint8_t row, const String& text,
                           uint16_t startHoldMs = 1200,
                           uint16_t stepMs = 250,
                           uint16_t endHoldMs = 1000) {
  int len = text.length();

  // Fits on one line: just show it
  if (len <= 16) {
    lcd.setCursor(0, row);
    lcd.print(text);
    return;
  }

  // 1. Show the START and hold
  lcd.setCursor(0, row);
  lcd.print(text.substring(0, 16));
  delay(startHoldMs);

  // 2. Scroll one character at a time
  for (int offset = 1; offset <= len - 16; offset++) {
    lcd.setCursor(0, row);
    lcd.print(text.substring(offset, offset + 16));
    delay(stepMs);
  }

  // 3. Hold on the END so it can be read
  delay(endHoldMs);
}

inline void flashThreeAnimation(hd44780_pinIO& lcd, const char* phrase = nullptr) {
  lcd.clear();
  putSprite(lcd, 15, 0, HOOP);

  for (uint8_t x = 0; x < 3; x++) {
    if (x) blankCell(lcd, x - 1, 1);
    putSprite(lcd, x, 1, (x % 2) ? RUN_B : RUN_A);
    delay(150);
  }

  blankCell(lcd, 2, 1);
  putSprite(lcd, 2, 1, SHOOT);
  delay(300);
  putSprite(lcd, 3, 0, BALL);
  delay(150);

  for (uint8_t x = 4; x <= 14; x++) {
    blankCell(lcd, x - 1, 0);
    putSprite(lcd, x, 0, BALL);
    delay(x < 9 ? 110 : 90);
  }
  blankCell(lcd, 14, 0);

  for (uint8_t i = 0; i < 6; i++) {
    putSprite(lcd, 15, 0, (i % 2) ? HOOP : HOOP_SHAKE);
    if (i == 1) putSprite(lcd, 15, 1, BALL);
    if (i == 3) blankCell(lcd, 15, 1);
    delay(110);
  }
  putSprite(lcd, 15, 0, HOOP);

  for (uint8_t i = 0; i < 2; i++) {
    blankCell(lcd, 2, 1);
    putSprite(lcd, 2, 0, AIR);
    delay(220);
    blankCell(lcd, 2, 0);
    putSprite(lcd, 2, 1, SHOOT);
    delay(220);
  }

  for (uint8_t i = 0; i < 3; i++) {
    lcd.clear();
    delay(120);
    centeredText(lcd, 0, "*** SPLASH! ***");
    centeredText(lcd, 1, phrase ? phrase : "SPLASH!");
    delay(300);
  }
}

inline void flashDunkAnimation(hd44780_pinIO& lcd) {
  lcd.clear();
  putSprite(lcd, 15, 0, HOOP);

  for (uint8_t x = 0; x < 8; x++) {
    if (x) blankCell(lcd, x - 1, 1);
    putSprite(lcd, x, 1, (x % 2) ? RUN_B : RUN_A);
    delay(180);
  }

  blankCell(lcd, 7, 1);
  putSprite(lcd, 9, 1, AIR);
  delay(210);
  blankCell(lcd, 9, 1);
  putSprite(lcd, 11, 0, AIR);
  delay(210);
  blankCell(lcd, 11, 0);
  putSprite(lcd, 13, 0, DUNK);
  putSprite(lcd, 14, 0, BALL);
  delay(300);

  blankCell(lcd, 14, 0);
  for (uint8_t i = 0; i < 6; i++) {
    putSprite(lcd, 15, 0, (i % 2) ? HOOP : HOOP_SHAKE);
    if (i == 1) putSprite(lcd, 15, 1, BALL);
    if (i == 3) blankCell(lcd, 15, 1);
    delay(110);
  }
  putSprite(lcd, 15, 0, HOOP);

  blankCell(lcd, 13, 0);
  putSprite(lcd, 13, 1, RUN_A);
  delay(350);

  for (uint8_t i = 0; i < 3; i++) {
    lcd.clear();
    delay(120);
    centeredText(lcd, 0, "*** SLAM! ***");
    centeredText(lcd, 1, "DUNK!");
    delay(300);
  }
  lcd.clear();
}

inline void triggerThreeCelebration(hd44780_pinIO& lcd, const String& player) {
  flashThreeAnimation(lcd);
  const char* quote = dickieV::pick(dickieV::THREE_QUOTES);
  const char* summary = dickieV::pick(dickieV::THREE_SUMMARIES);
  String line2 = player + " " + summary;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(quote);
  scrollTextLine(lcd, 1, line2);
  delay(1800);
}

inline void triggerDunkCelebration(hd44780_pinIO& lcd, const String& player) {
  flashDunkAnimation(lcd);
  const char* quote = dickieV::pick(dickieV::DUNK_QUOTES);
  const char* summary = dickieV::pick(dickieV::DUNK_SUMMARIES);
  String line2 = player + " " + summary;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(quote);
  scrollTextLine(lcd, 1, line2);
  delay(1800);
}
