#pragma once

namespace dickieV {

// Top line should be a short Dickie V quote. Bottom line should be
// player + quick summary, like: "WOWEE-ZOWEE" / "Boozer 3Pts".

const char* const THREE_QUOTES[] = {
  "WOWEE-ZOWEE",
  "AWESOME, BABY!",
  "ONIONS!",
  "DIAPER DANDY!",
  "A-W-E-S-O-M-E!",
  "DOWNTOWN, BABY!",
  "RAINBOW, BABY!",
  "HOLY TOLEDO!",
  "BANG-BANG, BABY!",
  "WOWEE ZOWEE!"
};

const char* const THREE_SUMMARIES[] = {
  "deep three",
  "lights out",
  "heat check",
  "from downtown",
  "cold-blooded",
  "long-range",
  "big bucket",
  "dagger shot",
  "deep ball",
  "pull-up shot"
};

const char* const DUNK_QUOTES[] = {
  "DUNKAROO!",
  "DIPSY-DO!",
  "HOLY TOLEDO!",
  "RIM RATTLER!",
  "WINDMILL, BABY!",
  "POSTER, BABY!",
  "STUFF IT!",
  "THAT WAS NASTY!",
  "SLAM TIME!",
  "WOWEE ZOWEE!"
};

const char* const DUNK_SUMMARIES[] = {
  "slam",
  "poster",
  "jam",
  "riser",
  "dunk",
  "throwdown",
  "flush",
  "crash"
};

const char* const BUZZER_QUOTES[] = {
  "NO WAY, BABY!",
  "UNBELIEVABLE!",
  "WHAT A FINISH!",
  "BANG-BANG, BABY!",
  "MARCH MADNESS!",
  "ONIONS!",
  "HOLY TOLEDO!",
  "SWEET JESUS!",
  "AMAZING!",
  "WOWEE-ZOWEE"
};

const char* const BUZZER_SUMMARIES[] = {
  "buzzer beater",
  "last-second",
  "game-winner",
  "at the horn",
  "clutch shot",
  "despair shot",
  "walkoff trey",
  "instant classic",
  "final horn",
  "hero shot"
};

template <size_t N>
const char* pick(const char* const (&arr)[N]) {
  return arr[random(N)];
}

}  // namespace dickieV
