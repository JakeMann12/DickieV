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

inline void flashThreeAnimation(hd44780_pinIO& lcd, const char* phrase = nullptr) {
  lcd.clear();
  putSprite(lcd, 15, 0, HOOP);

  for (uint8_t x = 0; x < 3; x++) {
    if (x) blankCell(lcd, x - 1, 1);
    putSprite(lcd, x, 1, (x % 2) ? RUN_B : RUN_A);
    delay(120);
  }

  blankCell(lcd, 2, 1);
  putSprite(lcd, 2, 1, SHOOT);
  delay(250);
  putSprite(lcd, 3, 0, BALL);
  delay(120);

  for (uint8_t x = 4; x <= 14; x++) {
    blankCell(lcd, x - 1, 0);
    putSprite(lcd, x, 0, BALL);
    delay(x < 9 ? 90 : 70);
  }
  blankCell(lcd, 14, 0);

  for (uint8_t i = 0; i < 6; i++) {
    putSprite(lcd, 15, 0, (i % 2) ? HOOP : HOOP_SHAKE);
    if (i == 1) putSprite(lcd, 15, 1, BALL);
    if (i == 3) blankCell(lcd, 15, 1);
    delay(90);
  }
  putSprite(lcd, 15, 0, HOOP);

  for (uint8_t i = 0; i < 2; i++) {
    blankCell(lcd, 2, 1);
    putSprite(lcd, 2, 0, AIR);
    delay(180);
    blankCell(lcd, 2, 0);
    putSprite(lcd, 2, 1, SHOOT);
    delay(180);
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
    delay(150);
  }

  blankCell(lcd, 7, 1);
  putSprite(lcd, 9, 1, AIR);
  delay(170);
  blankCell(lcd, 9, 1);
  putSprite(lcd, 11, 0, AIR);
  delay(170);
  blankCell(lcd, 11, 0);
  putSprite(lcd, 13, 0, DUNK);
  putSprite(lcd, 14, 0, BALL);
  delay(250);

  blankCell(lcd, 14, 0);
  for (uint8_t i = 0; i < 6; i++) {
    putSprite(lcd, 15, 0, (i % 2) ? HOOP : HOOP_SHAKE);
    if (i == 1) putSprite(lcd, 15, 1, BALL);
    if (i == 3) blankCell(lcd, 15, 1);
    delay(90);
  }
  putSprite(lcd, 15, 0, HOOP);

  blankCell(lcd, 13, 0);
  putSprite(lcd, 13, 1, RUN_A);
  delay(300);

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
  if (line2.length() > 16) {
    line2 = line2.substring(0, 16);
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(quote);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  delay(1800);
}

inline void triggerDunkCelebration(hd44780_pinIO& lcd, const String& player) {
  flashDunkAnimation(lcd);
  const char* quote = dickieV::pick(dickieV::DUNK_QUOTES);
  const char* summary = dickieV::pick(dickieV::DUNK_SUMMARIES);
  String line2 = player + " " + summary;
  if (line2.length() > 16) {
    line2 = line2.substring(0, 16);
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(quote);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  delay(1800);
}
