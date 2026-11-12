#pragma once

#include <Arduino.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>
#include "phrases.h"

// The HD44780 only has 8 custom-character slots (0-7). Anything above that
// silently wraps (8 -> 0, 9 -> 1 ...), which is why the old devil sprite
// stomped on the runner sprites and looked like garbage.
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

// ============================================================================
//  BIG 2x2 LETTERS, ONE AT A TIME, UNMOVING
// ============================================================================
//  Every glyph is a 10 px wide x 16 px tall bitmap = 2x2 LCD cells.
//  16 rows per glyph, bit 9 (leftmost digit) = leftmost pixel. 1 = lit.
//  At runtime each bitmap is sliced into four 5x8 tiles (TL, TR, BL, BR) and
//  loaded into CGRAM. Identical tiles share a slot; blank tiles use ' ' and
//  solid tiles use the built-in 0xFF block, so they cost no slots at all.
//
//  Letters are revealed one at a time, each at a fixed unmoving position (the
//  next letter sits 3 columns to the right). Slots are claimed once and never
//  overwritten, so letters already on screen stay correct as new ones appear.
//
//  Slot budget (only 8 exist): every distinct tile across a word's letters
//  shares the pool; if more than 8 distinct tiles are needed the extras
//  degrade gracefully to blank.
// ============================================================================

static const uint16_t BIG_D[16] = {
  0b1111111100, 0b1111111110, 0b1111111111, 0b1110000111,
  0b1110000111, 0b1110000111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1110000111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1111111111, 0b1111111110, 0b1111111100
};

static const uint16_t BIG_U[16] = {
  0b1110000111, 0b1110000111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1110000111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1110000111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1111111111, 0b0111111110, 0b0011111100
};

static const uint16_t BIG_K[16] = {
  0b1110000111, 0b1110000111, 0b1110001110, 0b1110011100,
  0b1110111000, 0b1111110000, 0b1111110000, 0b1111100000,
  0b1111100000, 0b1111110000, 0b1111110000, 0b1110111000,
  0b1110011100, 0b1110001110, 0b1110000111, 0b1110000111
};

static const uint16_t BIG_E[16] = {
  0b1111111111, 0b1111111111, 0b1111111111, 0b1110000000,
  0b1110000000, 0b1110000000, 0b1111111100, 0b1111111100,
  0b1111111100, 0b1110000000, 0b1110000000, 0b1110000000,
  0b1110000000, 0b1111111111, 0b1111111111, 0b1111111111
};

static const uint16_t BIG_M[16] = {
  0b1111001111, 0b1111001111, 0b1110110111, 0b1110110111,
  0b1110110111, 0b1110110111, 0b1110110111, 0b1110110111,
  0b1110110111, 0b1110110111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1110000111, 0b1110000111, 0b1110000111
};

static const uint16_t BIG_F[16] = {
  0b1111111111, 0b1111111111, 0b1111111111, 0b1110000000,
  0b1110000000, 0b1110000000, 0b1111111100, 0b1111111100,
  0b1111111100, 0b1110000000, 0b1110000000, 0b1110000000,
  0b1110000000, 0b1110000000, 0b1110000000, 0b1110000000
};

static const uint16_t BIG_G[16] = {
  0b0111111110, 0b1111111111, 0b1111111111, 0b1110000111,
  0b1110000111, 0b1110000000, 0b1110000000, 0b1110000000,
  0b1110011111, 0b1110011111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1111111111, 0b1111111111, 0b0111111110
};

static const uint16_t BIG_T[16] = {
  0b1111111111, 0b1111111111, 0b1111111111, 0b0001111000,
  0b0001111000, 0b0001111000, 0b0001111000, 0b0001111000,
  0b0001111000, 0b0001111000, 0b0001111000, 0b0001111000,
  0b0001111000, 0b0001111000, 0b0001111000, 0b0001111000
};

static const uint16_t BIG_H[16] = {
  0b1110000111, 0b1110000111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1110000111, 0b1111111111, 0b1111111111,
  0b1111111111, 0b1110000111, 0b1110000111, 0b1110000111,
  0b1110000111, 0b1110000111, 0b1110000111, 0b1110000111
};

static const uint16_t BIG_C[16] = {
  0b0111111110, 0b1111111111, 0b1111111111, 0b1110000111,
  0b1110000000, 0b1110000000, 0b1110000000, 0b1110000000,
  0b1110000000, 0b1110000000, 0b1110000000, 0b1110000000,
  0b1110000111, 0b1111111111, 0b1111111111, 0b0111111110
};

// Duke Blue Devil, facing right like the logo (horn, pointed ear, angry eye slit,
// grin, goatee). Drawn as the FACE, not the big "D" behind it.
//   ....#.....   horn
//   ...##.....
//   ..####....
//   .#######..
//   ##.######.   ear tip, gap, brow
//   ##.###..#.   eye
//   ###.#..##.
//   .##.######   nose starts
//   ...#######   nose tip
//   ...######.
//   ...###..#.   grin
//   ...######.
//   ....#####.   chin
//   ....####..   goatee
//   .....###..
//   ......##..
static const uint16_t BIG_DEVIL[16] = {
  0b0000010000, 0b0000110000, 0b0001111000, 0b0111111100,
  0b1101111110, 0b1101110010, 0b1110100110, 0b0110111111,
  0b0001111111, 0b0001111110, 0b0001110010, 0b0001111110,
  0b0000111110, 0b0000111100, 0b0000011100, 0b0000001100
};

inline const uint16_t* bigGlyph(char c) {
  switch (c) {
    case 'D': return BIG_D;
    case 'U': return BIG_U;
    case 'K': return BIG_K;
    case 'E': return BIG_E;
    case 'M': return BIG_M;
    case 'F': return BIG_F;
    case 'G': return BIG_G;
    case 'T': return BIG_T;
    case 'H': return BIG_H;
    case 'C': return BIG_C;
    default:  return nullptr;
  }
}

inline uint16_t reverse10(uint16_t v) {
  uint16_t r = 0;
  for (uint8_t i = 0; i < 10; i++) {
    r = (r << 1) | (v & 1);
    v >>= 1;
  }
  return r;
}

// Cut one 5x8 tile out of a 10x16 bitmap. tileCol/tileRow are 0 or 1.
inline void sliceTile(const uint16_t* bmp, bool mirror, uint8_t tileCol, uint8_t tileRow, uint8_t out[8]) {
  for (uint8_t r = 0; r < 8; r++) {
    uint16_t row = bmp[tileRow * 8 + r];
    if (mirror) row = reverse10(row);
    out[r] = tileCol ? (row & 0x1F) : ((row >> 5) & 0x1F);
  }
}

// Returns the character code to print for this tile, loading it into CGRAM if needed.
inline uint8_t tileToCode(hd44780_pinIO& lcd, const uint8_t tile[8],
                          uint8_t slots[8][8], uint8_t& used, uint8_t base) {
  bool blank = true, full = true;
  for (uint8_t r = 0; r < 8; r++) {
    if (tile[r] != 0) blank = false;
    if (tile[r] != 0x1F) full = false;
  }
  if (blank) return ' ';
  if (full) return 0xFF;

  for (uint8_t s = base; s < used; s++) {
    if (memcmp(slots[s], tile, 8) == 0) return s;
  }
  if (used >= 8) return ' ';  // out of slots, degrade gracefully

  memcpy(slots[used], tile, 8);
  lcd.createChar(used, slots[used]);
  return used++;
}

struct BigChunk {
  uint8_t n;          // letters in this chunk (max 2)
  uint8_t code[2][4]; // per letter: TL, TR, BL, BR
};

// Loads the tiles for up to 2 letters into CGRAM slots starting at `base`.
inline BigChunk loadChunk(hd44780_pinIO& lcd, const char* letters, uint8_t base) {
  BigChunk ch;
  memset(&ch, 0, sizeof(ch));
  uint8_t slots[8][8];
  uint8_t used = base;

  for (uint8_t i = 0; letters[i] && ch.n < 2; i++) {
    const uint16_t* g = bigGlyph(letters[i]);
    if (!g) continue;
    for (uint8_t t = 0; t < 4; t++) {
      uint8_t tile[8];
      sliceTile(g, false, t & 1, t >> 1, tile);
      ch.code[ch.n][t] = tileToCode(lcd, tile, slots, used, base);
    }
    ch.n++;
  }
  return ch;
}

// Devil always lives in slots 0-3 (TL, TR, BL, BR).
inline void loadDevil(hd44780_pinIO& lcd, bool mirrored) {
  for (uint8_t t = 0; t < 4; t++) {
    uint8_t tile[8];
    sliceTile(BIG_DEVIL, mirrored, t & 1, t >> 1, tile);
    lcd.createChar(t, tile);
  }
}

inline void drawCell(hd44780_pinIO& lcd, int col, uint8_t row, uint8_t code) {
  if (col < 0 || col > 15) return;  // clip so groups can slide on/off screen
  lcd.setCursor(col, row);
  lcd.write(code);
}

inline void blankColumn(hd44780_pinIO& lcd, int col) {
  drawCell(lcd, col, 0, ' ');
  drawCell(lcd, col, 1, ' ');
}

inline int chunkWidth(const BigChunk& ch) {
  return ch.n ? ch.n * 3 - 1 : 0;  // 2 cols per letter + 1 col gap
}

inline int groupWidth(const BigChunk& ch, bool devil) {
  return chunkWidth(ch) + (devil ? 3 : 0);  // devil = 2 cols + 1 col gap
}

// Draws [devil][gap][letters] or [letters][gap][devil] with its left edge at x.
inline void drawGroup(hd44780_pinIO& lcd, int x, const BigChunk& ch, bool devil, bool devilRight) {
  int chunkX = (devil && !devilRight) ? x + 3 : x;

  for (uint8_t i = 0; i < ch.n; i++) {
    int cx = chunkX + i * 3;
    drawCell(lcd, cx,     0, ch.code[i][0]);
    drawCell(lcd, cx + 1, 0, ch.code[i][1]);
    drawCell(lcd, cx,     1, ch.code[i][2]);
    drawCell(lcd, cx + 1, 1, ch.code[i][3]);
  }

  if (devil) {
    int dx = devilRight ? x + chunkWidth(ch) + 1 : x;
    drawCell(lcd, dx,     0, 0);
    drawCell(lcd, dx + 1, 0, 1);
    drawCell(lcd, dx,     1, 2);
    drawCell(lcd, dx + 1, 1, 3);
  }
}

inline void eraseGroup(hd44780_pinIO& lcd, int x, int width) {
  for (int c = x; c < x + width; c++) blankColumn(lcd, c);
}

// Slides the group one column at a time, only erasing the column it leaves behind.
inline void slideGroup(hd44780_pinIO& lcd, int fromX, int toX, const BigChunk& ch,
                       bool devil, bool devilRight, uint16_t stepMs) {
  int dir = (toX >= fromX) ? 1 : -1;
  int gw = groupWidth(ch, devil);

  for (int x = fromX;; x += dir) {
    drawGroup(lcd, x, ch, devil, devilRight);
    blankColumn(lcd, dir > 0 ? x - 1 : x + gw);
    delay(stepMs);
    if (x == toX) break;
  }
}

struct HypeWord {
  const char* letters;
  uint8_t letterCount;
};

// One entry per word: the letters are revealed one at a time at fixed,
// unmoving positions (see bigWordAnimation).
static const HypeWord HYPE_WORDS[] = {
  { "DUKE", 4 },
  { "DDMF", 4 },
  { "GTHC", 4 },
};
static const uint8_t HYPE_WORD_COUNT = sizeof(HYPE_WORDS) / sizeof(HYPE_WORDS[0]);

// Reveal the letters of a word one at a time, each at a fixed unmoving
// position (each letter sits 3 columns to the right of the previous one).
// All letters stay on screen once revealed; nothing slides or flies.
// Distinct tiles are pooled into the 8 CGRAM slots and never overwritten, so
// earlier letters remain correct while later ones are added.
inline void bigWordAnimation(hd44780_pinIO& lcd, const HypeWord& w,
                             uint16_t stepMs = 500, uint16_t holdMs = 800) {
  lcd.clear();
  uint8_t slots[8][8];
  uint8_t used = 0;

  for (uint8_t i = 0; i < w.letterCount; i++) {
    const uint16_t* g = bigGlyph(w.letters[i]);
    if (!g) continue;

    uint8_t code[4];
    for (uint8_t t = 0; t < 4; t++) {
      uint8_t tile[8];
      sliceTile(g, false, t & 1, t >> 1, tile);
      code[t] = tileToCode(lcd, tile, slots, used, 0);
    }

    int x = i * 3;  // fixed unmoving column (2 wide + 1 col gap)
    drawCell(lcd, x,     0, code[0]);
    drawCell(lcd, x + 1, 0, code[1]);
    drawCell(lcd, x,     1, code[2]);
    drawCell(lcd, x + 1, 1, code[3]);

    delay(stepMs);  // show one letter before revealing the next
  }

  delay(holdMs);  // hold the full word so it can be read
  lcd.clear();
}

// Cycles DUKE -> DDMF -> GTHC each time it's called
inline void preGameHype(hd44780_pinIO& lcd) {
  static uint8_t idx = 0;

  const HypeWord& w = HYPE_WORDS[idx];
  bigWordAnimation(lcd, w);

  idx = (idx + 1) % HYPE_WORD_COUNT;

  // The hype animation took over CGRAM, so give the dunk/three sprites back.
  initDunkSprites(lcd);
}

// ============================================================================
//  Everything below is unchanged
// ============================================================================

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