#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>
#include <time.h>
#include "phrases.h"
#include "animations.h"
#include "wifi_debug.h"
const char* HOST = "site.api.espn.com";
const char* LEAGUE_PATH = "/apis/site/v2/sports/basketball/mens-college-basketball";

// The tracked team. espnId is the numeric ESPN team id (the number in the
// team's espn.com URL); label is what the LCD shows (keep it <= 5 chars).
struct Team {
  const char* espnId;
  const char* label;
};
static const Team MY_TEAM = {"150", "DUKE"};

const unsigned long GAME_START_CHECK_INTERVAL_MS = 30UL * 1000UL;
// Faster polling for crunch time: final 5 minutes of the 2nd half or any
// overtime, with the margin within 15 points.
const unsigned long CLOSE_GAME_CHECK_INTERVAL_MS = 5UL * 1000UL;
const int CLOSE_GAME_CLOCK_SECS = 5 * 60;
const int CLOSE_GAME_MARGIN = 15;
// How often an upcoming game is re-validated (catches time changes).
const unsigned long NEXT_GAME_RECHECK_INTERVAL_MS = 60UL * 60UL * 1000UL;
// How often to look again when the team has no games left on the schedule.
const unsigned long NO_GAME_RECHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
// A game this long past its start that ESPN can't return is dropped.
const long STALE_GAME_SECS = 6L * 60L * 60L;
// How often the countdown text is re-evaluated. The LCD is only rewritten when
// the displayed text actually changes (i.e. once per second).
const unsigned long COUNTDOWN_TICK_MS = 200UL;

//Seeed XIAO ESP32‑C5	Likely under $10	Small and cheap. I haven't confirmed the current price.////
//BW16 (Realtek RTL8720DN)	About $5–8 on AliExpress (from memory, not checked)	Cheapest, but its Arduino support and documentation are much weaker. OTA and HTTPS would take more work.

// Firmware release number (GitHub release tag / run number — e.g. "24").
const char* FIRMWARE_VERSION = "0";

// Button wired from D4 to GND. Uses the internal pullup so the pin reads
// HIGH when open and LOW when the button is pressed.
const uint8_t BUTTON_PIN = D4;

struct BootCache {
  uint32_t magic;
  char teamId[8];
  char lastConnectedSsid[32];
  char eventId[16];
  char startIso[24];
  char opponent[12];
};

// Bump when BootCache's layout changes so stale EEPROM data is ignored.
static const uint32_t BOOT_CACHE_MAGIC = 0x44524458u;

// The game being tracked: the live/final game today, or else the next one.
struct Game {
  String eventId;   // empty = no game known
  String startIso;  // e.g. "2026-11-26T00:00Z"
  String state;     // ESPN state: "pre", "in", "post" ("" = unknown yet)
  String opponent;  // e.g. "vs UNC" (home/neutral) or "@ UVA" (away)
  String scoreLine; // e.g. "DUKE 45 UNC 39"
  String detail;    // e.g. "14:44 - 2nd", "Final"
  int period = 0;   // 1 = 1st half, 2 = 2nd half, 3+ = overtime
  int clockSecs = -1;
  int margin = -1;  // absolute point difference (-1 = unknown)
};
static Game g_game;
static unsigned long lastGameStartCheckMs = 0;
// millis() of the last successful next-game lookup (0 = never).
static unsigned long lastNextGameCheckMs = 0;
static unsigned long lastCountdownRefreshMs = 0;
static unsigned long lastNoGameHypeMs = 0;
static BootCache bootCache = {};

// How often to cycle the hype animations when there's no game to show.
const unsigned long NO_GAME_HYPE_INTERVAL_MS = 120UL * 1000UL;

// Button press latch. loop() is often blocked for seconds (ESPN fetch, hype
// animations), so a falling-edge interrupt records the press until loop() can
// handle it; otherwise short taps between polls are missed.
const unsigned long BUTTON_DEBOUNCE_MS = 200UL;
static volatile bool g_buttonPressed = false;
static volatile unsigned long g_lastButtonEdgeMs = 0;

IRAM_ATTR void onButtonFalling() {
  unsigned long nowMs = millis();
  if (nowMs - g_lastButtonEdgeMs < BUTTON_DEBOUNCE_MS) return;
  g_lastButtonEdgeMs = nowMs;
  g_buttonPressed = true;
}

// OTA update config.
static unsigned long lastUpdateCheckMs = 0;
const unsigned long UPDATE_CHECK_INTERVAL_MS = 60UL * 60UL * 1000UL; // hourly check
const String githubOwner = "JakeMann12";
const String githubRepo = "DickieV";

hd44780_pinIO lcd(D1, D2, D5, D6, D7, D0);

String safeLcdLine(const String& s) {
  String out = s;
  if (out.length() > 16) {
    out = out.substring(0, 16);
  }
  return out;
}

// Text most recently written by showOnLcd(), so the countdown can rewrite just
// the second row instead of clearing (and flickering) the whole display.
static String g_lcdLine1;
static String g_lcdLine2;

void showOnLcd(const String& l1, const String& l2) {
  Serial.println(l1);
  Serial.println(l2);
  g_lcdLine1 = l1;
  g_lcdLine2 = l2;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(safeLcdLine(l1));
  lcd.setCursor(0, 1);
  lcd.print(safeLcdLine(l2));
}

static time_t parseIsoUtc(const String& iso) {
  int y, mo, d, h, mi, s = 0;
  int n = sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s);
  if (n < 5) return 0;   // seconds are optional

  // days since 1970-01-01 (civil-from-days algorithm)
  y -= (mo <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = y - era * 400;
  unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;

  return (time_t)(days * 86400L + h * 3600L + mi * 60L + s);
}

static String formatCountdown(const String& iso) {
  time_t startUtc = parseIsoUtc(iso);
  if (startUtc == 0) return "T- --:--";

  time_t nowUtc = time(nullptr);
  long diff = startUtc - nowUtc;
  if (diff < 0) return "Tip-off soon";

  long hours = diff / 3600;
  long minutes = (diff % 3600) / 60;
  long seconds = diff % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "T-%02ld:%02ld:%02ld", hours, minutes, seconds);
  return String(buf);
}

static long secondsUntil(const String& iso) {
  time_t startUtc = parseIsoUtc(iso);
  if (startUtc == 0) return -1;
  return (long)(startUtc - time(nullptr));
}

static String makePstDateString();

// "vs UNC" / "@ UVA", with the ESPN abbreviation trimmed only as far as needed
// for matchupLine() to fit on one 16-char LCD row.
static String opponentLabel(const String& abbr, bool away) {
  if (abbr.length() == 0) return "";
  String prefix = away ? "@ " : "vs ";
  int room = 16 - (int)strlen(MY_TEAM.label) - 1 - (int)prefix.length();
  return prefix + abbr.substring(0, room > 0 ? room : 0);
}

static String matchupLine(const String& opponent) {
  String team = MY_TEAM.label;
  return opponent.length() > 0 ? team + " " + opponent : team + " game";
}

static String noGameLine() {
  return String("No ") + MY_TEAM.label + " game";
}

static String makePstDateKeyFromUtc(time_t utcEpoch) {
  time_t pstEpoch = utcEpoch - (8 * 3600L);
  struct tm* pst = gmtime(&pstEpoch);

  String year = String(pst->tm_year + 1900);
  String month = String(pst->tm_mon + 1);
  String day = String(pst->tm_mday);

  if (month.length() == 1) {
    month = "0" + month;
  }
  if (day.length() == 1) {
    day = "0" + day;
  }

  return year + month + day;
}

static String makePstDateString() {
  time_t nowUtc = time(nullptr);
  return makePstDateKeyFromUtc(nowUtc);
}

static bool readBootCache() {
  EEPROM.begin(sizeof(BootCache));
  EEPROM.get(0, bootCache);
  EEPROM.end();
  if (bootCache.magic != BOOT_CACHE_MAGIC) {
    memset(&bootCache, 0, sizeof(bootCache));
    return false;
  }
  bootCache.eventId[sizeof(bootCache.eventId) - 1] = '\0';
  bootCache.startIso[sizeof(bootCache.startIso) - 1] = '\0';
  bootCache.opponent[sizeof(bootCache.opponent) - 1] = '\0';
  bootCache.teamId[sizeof(bootCache.teamId) - 1] = '\0';
  bootCache.lastConnectedSsid[sizeof(bootCache.lastConnectedSsid) - 1] = '\0';
  return true;
}

// Persists the last WiFi network and the tracked game so a reboot can show
// the countdown without waiting on ESPN.
static void writeBootCache(const String& ssid) {
  memset(&bootCache, 0, sizeof(bootCache));
  bootCache.magic = BOOT_CACHE_MAGIC;
  strlcpy(bootCache.teamId, MY_TEAM.espnId, sizeof(bootCache.teamId));
  strlcpy(bootCache.lastConnectedSsid, ssid.c_str(), sizeof(bootCache.lastConnectedSsid));
  strlcpy(bootCache.eventId, g_game.eventId.c_str(), sizeof(bootCache.eventId));
  strlcpy(bootCache.startIso, g_game.startIso.c_str(), sizeof(bootCache.startIso));
  strlcpy(bootCache.opponent, g_game.opponent.c_str(), sizeof(bootCache.opponent));
  EEPROM.begin(sizeof(BootCache));
  EEPROM.put(0, bootCache);
  EEPROM.commit();
  EEPROM.end();
}

// WiFi networks, parsed at boot from WIFI_SECRETS_BYTES which scripts/pre_build.py
// injects from the WIFI_SECRETS repo secret / env var ("SSID|PASSWORD" per line).
struct WifiNetwork {
  String ssid;
  String password;
};

static const size_t MAX_WIFI_NETWORKS = 8;
static WifiNetwork wifiNetworks[MAX_WIFI_NETWORKS];
static size_t wifiNetworkCount = 0;

static void loadWifiNetworks() {
  wifiNetworkCount = 0;
#ifdef WIFI_SECRETS_BYTES
  static const char secrets[] = { WIFI_SECRETS_BYTES, 0 };
  String all(secrets);
  int start = 0;
  while (start < (int)all.length() && wifiNetworkCount < MAX_WIFI_NETWORKS) {
    int end = all.indexOf('\n', start);
    if (end < 0) end = all.length();
    int sep = all.indexOf('|', start);
    if (sep > start && sep < end) {
      wifiNetworks[wifiNetworkCount].ssid = all.substring(start, sep);
      wifiNetworks[wifiNetworkCount].password = all.substring(sep + 1, end);
      ++wifiNetworkCount;
    }
    start = end + 1;
  }
#else
  Serial.println("WARNING: built without WIFI_SECRETS; no WiFi networks configured");
#endif
}

// Non-blocking WiFi connection.
// beginWifiConnect() picks the preferred-first network order and starts the
// first attempt. pollWifiConnect() must be called repeatedly; it returns true
// once connected, false while still trying or once every network has been tried.
// The ESP8266 WiFi stack keeps attempting the connection in the background
// (during delay()/yield()), so the caller is free to keep the LCD animated.
static size_t g_wifiOrder[MAX_WIFI_NETWORKS];
static size_t g_wifiOrderCount = 0;
static size_t g_wifiIndex = 0;
static unsigned long g_wifiStartMs = 0;
static bool g_wifiActive = false;
static bool g_wifiExhausted = false;

static const unsigned long WIFI_ATTEMPT_TIMEOUT_MS = 10000UL;

static const char* wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:     return "idle/no reply";
    case WL_NO_SSID_AVAIL:   return "SSID not found";
    case WL_CONNECT_FAILED:  return "connect failed";
    case WL_WRONG_PASSWORD:  return "wrong password";
    case WL_CONNECTION_LOST: return "conn lost";
    case WL_DISCONNECTED:    return "disconnected";
    case WL_CONNECTED:       return "connected";
    default:                 return "unknown";
  }
}

static void attemptWifiNetwork(size_t idx) {
  const WifiNetwork& network = wifiNetworks[g_wifiOrder[idx]];
  Serial.print("Trying: ");
  Serial.println(network.ssid);
  wifiDebug::showAttempt(idx, g_wifiOrderCount, network.ssid, network.password);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(network.ssid, network.password);
  g_wifiStartMs = millis();
}

static void beginWifiConnect() {
  const size_t totalNetworks = wifiNetworkCount;
  g_wifiOrderCount = 0;
  g_wifiActive = false;
  g_wifiExhausted = false;
  if (totalNetworks == 0) {
    return;
  }

  int preferredIndex = -1;
  if (bootCache.lastConnectedSsid[0] != '\0') {
    for (size_t i = 0; i < totalNetworks; ++i) {
      if (wifiNetworks[i].ssid == String(bootCache.lastConnectedSsid)) {
        preferredIndex = static_cast<int>(i);
        break;
      }
    }
  }

  if (preferredIndex >= 0) {
    g_wifiOrder[g_wifiOrderCount++] = static_cast<size_t>(preferredIndex);
  }
  for (size_t i = 0; i < totalNetworks; ++i) {
    if (static_cast<int>(i) != preferredIndex) {
      g_wifiOrder[g_wifiOrderCount++] = i;
    }
  }

  g_wifiIndex = 0;
  g_wifiActive = true;
  attemptWifiNetwork(g_wifiIndex);
}

// Returns true once connected. Non-blocking: call repeatedly.
static bool pollWifiConnect() {
  if (!g_wifiActive || g_wifiExhausted) {
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    String connectedSsid = WiFi.SSID();
    Serial.print("Connected to ");
    Serial.println(connectedSsid);
    writeBootCache(connectedSsid);
    g_wifiActive = false;
    return true;
  }

  if (millis() - g_wifiStartMs >= WIFI_ATTEMPT_TIMEOUT_MS) {
    wl_status_t status = WiFi.status();
    const WifiNetwork& failed = wifiNetworks[g_wifiOrder[g_wifiIndex]];
    Serial.printf("Failed: %s (status %d: %s)\n", failed.ssid.c_str(),
                  static_cast<int>(status), wifiStatusText(status));
    wifiDebug::showFailure(failed.ssid, status, wifiStatusText(status));
    if (++g_wifiIndex >= g_wifiOrderCount) {
      g_wifiActive = false;
      g_wifiExhausted = true;
      return false;
    }
    attemptWifiNetwork(g_wifiIndex);
  }

  return false;
}

static bool isUpcoming(const Game& g) {
  return g.eventId.length() > 0 && g.state != "in" && g.state != "post";
}

// Final 5 minutes of the 2nd half (or any OT) with a margin within 15.
static bool isCloseLateGame(const Game& g) {
  return g.state == "in" && g.period >= 2 && g.clockSecs >= 0 &&
         g.clockSecs <= CLOSE_GAME_CLOCK_SECS && g.margin >= 0 &&
         g.margin <= CLOSE_GAME_MARGIN;
}

static void refreshCountdownDisplay() {
  if (!isUpcoming(g_game)) return;

  String line1 = matchupLine(g_game.opponent);
  String line2 = formatCountdown(g_game.startIso);

  if (line1 != g_lcdLine1) {
    showOnLcd(line1, line2);
    return;
  }
  if (line2 == g_lcdLine2) return;

  // Only the countdown changed: overwrite row 2 in place (padded to clear any
  // leftover characters) so the display ticks without flicker.
  g_lcdLine2 = line2;
  String padded = safeLcdLine(line2);
  while (padded.length() < 16) padded += ' ';
  lcd.setCursor(0, 1);
  lcd.print(padded);
}

// Keeps the countdown ticking locally from the last known game start time.
// Called from loop() and while an ESPN download is being parsed.
static void tickCountdown() {
  if (!isUpcoming(g_game)) return;
  unsigned long nowMs = millis();
  if (nowMs - lastCountdownRefreshMs < COUNTDOWN_TICK_MS) return;
  lastCountdownRefreshMs = nowMs;
  refreshCountdownDisplay();
}

static void showNoGame() {
  showOnLcd(noGameLine(), String("today v") + String(FIRMWARE_VERSION));
}

static void showGame() {
  if (g_game.eventId.length() == 0) {
    showNoGame();
  } else if (isUpcoming(g_game)) {
    showOnLcd(matchupLine(g_game.opponent), formatCountdown(g_game.startIso));
  } else {
    showOnLcd(g_game.scoreLine, g_game.detail);
  }
}

// Stream wrapper that keeps the countdown ticking while ArduinoJson reads an
// HTTPS response, which can otherwise block loop() for seconds.
class CountdownTickingStream : public Stream {
 public:
  explicit CountdownTickingStream(Stream& inner) : inner_(inner) {}
  int available() override { return inner_.available(); }
  int read() override {
    tickCountdown();
    return inner_.read();
  }
  int peek() override { return inner_.peek(); }
  size_t readBytes(char* buffer, size_t length) override {
    tickCountdown();
    return inner_.readBytes(buffer, length);
  }
  size_t write(uint8_t) override { return 0; }

 private:
  Stream& inner_;
};

// ---------------------------------------------------------------------------
// ESPN access. Every request is scoped to MY_TEAM (or one of its games):
//   teams/{id}            ~16 KB  next not-yet-started game ("nextEvent")
//   scoreboard/{eventId}  ~14 KB  live/final score + clock for one game
//   teams/{id}/schedule  ~350 KB  whole season; only when nothing is known
// ---------------------------------------------------------------------------

// GETs an ESPN path and parses it through `filter`. Returns false on any
// network, HTTP or JSON error.
static bool fetchEspnJson(const String& path, JsonDocument& filter, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(1024, 512);
  // Default 1 s stream timeout aborts large responses on brief network stalls.
  client.setTimeout(10000);

  if (!client.connect(HOST, 443)) {
    Serial.printf("Connect fail: %s\n", path.c_str());
    return false;
  }

  client.print(String("GET ") + LEAGUE_PATH + path + " HTTP/1.0\r\nHost: " + HOST +
               "\r\nConnection: close\r\n\r\n");
  String statusLine = client.readStringUntil('\n');
  if (statusLine.indexOf(" 200") < 0 || !client.find("\r\n\r\n")) {
    Serial.printf("Bad response for %s: %s\n", path.c_str(), statusLine.c_str());
    client.stop();
    return false;
  }

  CountdownTickingStream tickingClient(client);
  DeserializationError err = deserializeJson(
    doc,
    tickingClient,
    DeserializationOption::Filter(filter),
    DeserializationOption::NestingLimit(32));
  client.stop();
  if (err) {
    Serial.printf("JSON err for %s: %s\n", path.c_str(), err.c_str());
    return false;
  }
  return true;
}

// Fields of an ESPN event object that parseEvent() reads. The same shape is
// used by teams/{id} nextEvent, teams/{id}/schedule events and scoreboard/{id}.
static void addEventFilter(JsonObject f) {
  f["id"] = true;
  f["date"] = true;
  JsonObject comp = f["competitions"][0].to<JsonObject>();
  comp["neutralSite"] = true;
  comp["status"]["type"]["state"] = true;
  comp["status"]["type"]["shortDetail"] = true;
  comp["status"]["period"] = true;
  comp["status"]["clock"] = true;
  JsonObject competitor = comp["competitors"][0].to<JsonObject>();
  competitor["id"] = true;
  competitor["homeAway"] = true;
  competitor["score"] = true;
  competitor["team"]["abbreviation"] = true;
}

static Game parseEvent(JsonObject ev) {
  Game g;
  g.eventId = ev["id"] | "";
  g.startIso = ev["date"] | "";
  JsonObject comp = ev["competitions"][0];
  g.state = comp["status"]["type"]["state"] | "";
  g.detail = comp["status"]["type"]["shortDetail"] | "";
  g.period = comp["status"]["period"] | 0;
  g.clockSecs = (int)(comp["status"]["clock"] | -1.0f);
  bool neutral = comp["neutralSite"] | false;

  bool away = false;
  String oppAbbr = "?";
  String myScore = "0";
  String oppScore = "0";
  for (JsonObject c : comp["competitors"].as<JsonArray>()) {
    // scoreboard/{id} gives "45"; the schedule gives {"displayValue": "45"}.
    JsonVariant s = c["score"];
    String score = s.is<JsonObject>() ? String(s["displayValue"] | "0") : String(s | "0");
    if (String(c["id"] | "") == MY_TEAM.espnId) {
      away = String(c["homeAway"] | "") == "away";
      myScore = score;
    } else {
      oppAbbr = c["team"]["abbreviation"] | "?";
      oppScore = score;
    }
  }

  g.opponent = opponentLabel(oppAbbr, away && !neutral);
  g.margin = abs(myScore.toInt() - oppScore.toInt());
  // "DUKE 45 UNC 39", trimming only the opponent abbreviation to fit 16 chars.
  String mine = String(MY_TEAM.label) + " " + myScore + " ";
  int room = 16 - (int)mine.length() - 1 - (int)oppScore.length();
  g.scoreLine = mine + oppAbbr.substring(0, room > 1 ? room : 1) + " " + oppScore;
  return g;
}

// Current status/score of one game.
static bool fetchGame(const String& eventId, Game& out) {
  JsonDocument filter;
  addEventFilter(filter.to<JsonObject>());
  JsonDocument doc;
  // Unique query string skips ESPN's CDN cache, which can lag the game clock.
  String path = String("/scoreboard/") + eventId + "?_=" + String((unsigned long)time(nullptr));
  if (!fetchEspnJson(path, filter, doc)) return false;
  Game g = parseEvent(doc.as<JsonObject>());
  if (g.eventId.length() == 0) return false;
  out = g;
  return true;
}

// The team's next game that has not started. ESPN drops a game from nextEvent
// once it tips off, so this can't be used to find a game in progress.
// out.eventId is empty when no games remain.
static bool fetchNextGame(Game& out) {
  JsonDocument filter;
  addEventFilter(filter["team"]["nextEvent"][0].to<JsonObject>());
  JsonDocument doc;
  if (!fetchEspnJson(String("/teams/") + MY_TEAM.espnId, filter, doc)) return false;
  out = Game();
  for (JsonObject ev : doc["team"]["nextEvent"].as<JsonArray>()) {
    Game g = parseEvent(ev);
    if (g.state == "pre") {
      out = g;
      break;
    }
  }
  return true;
}

// Picks a game in progress, else one that finished today (PST), else the
// next upcoming game. Used only when nothing is known (e.g. first boot).
static bool fetchCurrentGameFromSchedule(Game& out) {
  JsonDocument filter;
  addEventFilter(filter["events"][0].to<JsonObject>());
  JsonDocument doc;
  if (!fetchEspnJson(String("/teams/") + MY_TEAM.espnId + "/schedule", filter, doc)) {
    return false;
  }

  String todayKey = makePstDateString();
  Game finishedToday, next;
  time_t nextStart = 0;
  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    Game g = parseEvent(ev);
    time_t start = parseIsoUtc(g.startIso);
    if (g.state == "in") {
      out = g;
      return true;
    }
    if (g.state == "post" && makePstDateKeyFromUtc(start) == todayKey) {
      finishedToday = g;
    } else if (g.state == "pre" && start != 0 && (nextStart == 0 || start < nextStart)) {
      next = g;
      nextStart = start;
    }
  }
  out = finishedToday.eventId.length() > 0 ? finishedToday : next;
  return true;
}

// Called every GAME_START_CHECK_INTERVAL_MS. Downloads only what the current
// phase needs:
//   upcoming game  -> nothing (countdown is local); teams/{id} hourly
//   game started   -> scoreboard/{eventId} every call until it's final
//   game final     -> nothing until the next PST day, then teams/{id}
//   nothing known  -> teams/{id}/schedule
void fetchScore() {
  unsigned long nowMs = millis();

  if (g_game.eventId.length() == 0) {
    if (lastNextGameCheckMs != 0 && nowMs - lastNextGameCheckMs < NO_GAME_RECHECK_INTERVAL_MS) {
      return;
    }
    Game found;
    if (!fetchCurrentGameFromSchedule(found)) {
      showNoGame();
      return;
    }
    lastNextGameCheckMs = nowMs;
    g_game = found;
    writeBootCache(WiFi.SSID());
  }

  if (isUpcoming(g_game) && secondsUntil(g_game.startIso) > 0) {
    if (lastNextGameCheckMs == 0 || nowMs - lastNextGameCheckMs >= NEXT_GAME_RECHECK_INTERVAL_MS) {
      Game next;
      if (fetchNextGame(next)) {
        lastNextGameCheckMs = nowMs;
        if (next.eventId.length() > 0 &&
            (next.eventId != g_game.eventId || next.startIso != g_game.startIso)) {
          g_game = next;
          writeBootCache(WiFi.SSID());
        }
      }
    }
    long secs = secondsUntil(g_game.startIso);
    if (secs > 0 && secs <= 120) {
      preGameHype(lcd);
    }
  } else if (g_game.state != "post") {
    // Start time reached (or state unknown after a reboot): poll this game.
    Game latest;
    if (fetchGame(g_game.eventId, latest)) {
      g_game = latest;
    } else if (secondsUntil(g_game.startIso) < -STALE_GAME_SECS) {
      // Long past start and ESPN won't return it (event removed or id
      // changed): forget it so the next call rediscovers from the schedule.
      g_game = Game();
      lastNextGameCheckMs = 0;
    }
  }

  if (g_game.state == "post" &&
      makePstDateKeyFromUtc(parseIsoUtc(g_game.startIso)) < makePstDateString()) {
    // Yesterday's final: move on to the next game.
    Game next;
    if (fetchNextGame(next)) {
      lastNextGameCheckMs = nowMs;
      g_game = next;
      writeBootCache(WiFi.SSID());
    }
  }

  Serial.printf("game id=%s state=%s start=%s %s | %s\n", g_game.eventId.c_str(),
                g_game.state.c_str(), g_game.startIso.c_str(), g_game.scoreLine.c_str(),
                g_game.detail.c_str());
  showGame();
}


// Very simple semver compare (handles tags like "v1.2.3").
// Example: 1.10.0 > 1.9.0
bool isNewerVersion(String newTag, String currentTag) {
  newTag.replace("v", "");
  currentTag.replace("v", "");

  int aMajor = 0, aMinor = 0, aPatch = 0;
  int bMajor = 0, bMinor = 0, bPatch = 0;

  sscanf(newTag.c_str(), "%d.%d.%d", &aMajor, &aMinor, &aPatch);
  sscanf(currentTag.c_str(), "%d.%d.%d", &bMajor, &bMinor, &bPatch);

  if (aMajor != bMajor) return aMajor > bMajor;
  if (aMinor != bMinor) return aMinor > bMinor;
  return aPatch > bPatch;
}

static void logHeap(const char* label) {
  Serial.printf("[heap] %s: free=%u maxBlock=%u frag=%u%%\n", label, ESP.getFreeHeap(),
                ESP.getMaxFreeBlockSize(), ESP.getHeapFragmentation());
}

// Query the GitHub "latest release" API. Kept in its own function so that the
// TLS client, HTTPClient and JsonDocument are all destroyed before OTA starts:
// the OTA download needs ~17 KB of contiguous heap for BearSSL's RX buffer.
static bool fetchLatestRelease(String& latestTag, String& assetUrl) {
  WiFiClientSecure client;
  client.setInsecure(); // tradeoff: accept self-signed / no cert validation

  HTTPClient http;
  // HTTP/1.0 avoids chunked transfer encoding so the body can be stream-parsed.
  http.useHTTP10(true);
  String url = "https://api.github.com/repos/" + githubOwner + "/" + githubRepo + "/releases/latest";
  http.begin(client, url);
  http.addHeader("User-Agent", "DickieV-ESP8266");
  http.addHeader("Accept", "application/vnd.github+json");

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("GitHub API failed: %d\n", httpCode);
    http.end();
    // 403 = GitHub rate limit (60/hr per IP); negative = network/TLS error.
    showOnLcd("OTA: GitHub err", String(httpCode) + (httpCode == 403 ? " rate limit" : ""));
    delay(3000);
    return false;
  }

  // Only keep the two fields we need instead of buffering the full release
  // JSON (body, author, asset metadata...) in RAM.
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["browser_download_url"] = true;

  JsonDocument doc;
  DeserializationError err =
    deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("JSON parse failed: %s\n", err.c_str());
    showOnLcd("OTA: JSON err", err.c_str());
    delay(3000);
    return false;
  }

  latestTag = doc["tag_name"] | "";
  assetUrl = doc["assets"][0]["browser_download_url"] | "";
  return true;
}

// Check GitHub releases for a newer firmware binary and OTA-update if found.
// Based on the project's OTA inspiration snippet.
bool checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("OTA check skipped: not connected to WiFi");
    return false;
  }

  logHeap("before release check");

  String latestTag;
  String assetUrl;
  if (!fetchLatestRelease(latestTag, assetUrl)) {
    return false;
  }

  if (latestTag.length() == 0 || assetUrl.length() == 0) {
    Serial.println("No release asset found");
    showOnLcd("OTA: no release", "asset found");
    delay(3000);
    return false;
  }

  Serial.printf("Current: %s | Latest: %s\n", FIRMWARE_VERSION, latestTag.c_str());

  if (!isNewerVersion(latestTag, FIRMWARE_VERSION)) {
    showOnLcd("Up to date", "v" + String(FIRMWARE_VERSION));
    delay(1200);
    Serial.println("No update needed");
    return false;
  }

  // A newer version exists — show what we're moving to, then flash it.
  showOnLcd("Update", latestTag);
  delay(1200);
  showOnLcd("loading...", "flashing...");
  Serial.println("Starting OTA...");

  WiFiClientSecure dlClient;
  dlClient.setInsecure();
  dlClient.setTimeout(30000);

  ESPhttpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  ESPhttpUpdate.rebootOnUpdate(true);

  logHeap("before OTA");
  t_httpUpdate_return ret = ESPhttpUpdate.update(dlClient, assetUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED: {
      Serial.printf("OTA failed: %s\n", ESPhttpUpdate.getLastErrorString().c_str());
      showOnLcd("OTA failed", ESPhttpUpdate.getLastErrorString());
      delay(2000);
      char sslErr[128];
      int sslCode = dlClient.getLastSSLError(sslErr, sizeof(sslErr));
      Serial.printf("SSL error %d: %s\n", sslCode, sslErr);
      logHeap("after OTA fail");
      return false;
    }
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("No update available");
      return false;
    case HTTP_UPDATE_OK:
      Serial.println("OTA OK");
      return true;
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("DickieV firmware %s\n", FIRMWARE_VERSION);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonFalling, FALLING);

  int status = lcd.begin(16, 2);
  if (status) {
    hd44780::fatalError(status);
  }
  initDunkSprites(lcd);

  readBootCache();
  if (bootCache.magic == BOOT_CACHE_MAGIC && String(bootCache.teamId) == MY_TEAM.espnId &&
      bootCache.eventId[0] != '\0') {
    // Load before WiFi connects: pollWifiConnect() rewrites the cache from g_game.
    g_game.eventId = bootCache.eventId;
    g_game.startIso = bootCache.startIso;
    g_game.opponent = bootCache.opponent;
  }
  loadWifiNetworks();

#if !WIFI_DEBUG_LCD
  flashDunkAnimation(lcd);
  flashThreeAnimation(lcd);
#endif

  // Brief version splash so you can see which firmware is running.
  showOnLcd("DickieV", "v" + String(FIRMWARE_VERSION));
  delay(1000);

  if (!wifiDebug::bootDiagnostics(wifiNetworks, wifiNetworkCount)) {
    return;
  }

  // Kick off the WiFi connection attempt in the background. The startup hype
  // animation keeps running on the LCD while pollWifiConnect() drives the
  // connection attempt under the hood between animation frames.
  beginWifiConnect();
  bool connected = false;
  if (g_wifiActive) {
#if WIFI_DEBUG_LCD
    // No overall deadline: every configured network gets a full attempt.
    while (!connected && !g_wifiExhausted) {
      delay(100);
      connected = pollWifiConnect();
    }
#else
    unsigned long connectDeadline = millis() + 40000UL;
    while (!connected && !g_wifiExhausted && millis() < connectDeadline) {
      preGameHype(lcd);          // startup animation keeps displaying
      connected = pollWifiConnect();  // non-blocking; WiFi connects under the hood
    }
#endif
  }

  if (connected) {
#if WIFI_DEBUG_LCD
    wifiDebug::showConnected();
#else
    showOnLcd("WiFi connected", WiFi.localIP().toString());
    delay(1000);
#endif
  }

  if (!connected) {
    showOnLcd("WiFi failed", "check networks");
    return;
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  unsigned long ntpStart = millis();
  while (time(nullptr) < 1700000000 && millis() - ntpStart < 20000) {
    delay(250);
  }
  wifiDebug::showNtpResult();

  if (g_game.eventId.length() > 0) {
    // Show the cached game right away; loop() then verifies it with ESPN.
    showGame();
  } else {
    fetchScore();
  }
}

void loop() {
  unsigned long nowMs = millis();

  // Button on D4 (INPUT_PULLUP, button to GND) latched by onButtonFalling().
  if (g_buttonPressed) {
    // Button only does something when we're connected to WiFi.
    if (WiFi.status() == WL_CONNECTED) {
      showOnLcd("Check update", "now...");
      delay(600);  // let the user see the button press registered
      checkForUpdate();
    } else {
      showOnLcd("WiFi not", "connected");
      delay(1200);
    }
    // Drop any presses made while the check was running.
    g_buttonPressed = false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    unsigned long checkIntervalMs = isCloseLateGame(g_game) ? CLOSE_GAME_CHECK_INTERVAL_MS
                                                            : GAME_START_CHECK_INTERVAL_MS;
    if (lastGameStartCheckMs == 0 ||
        nowMs - lastGameStartCheckMs >= checkIntervalMs) {
      lastGameStartCheckMs = nowMs;
      fetchScore();
    }

    tickCountdown();

    // When there's no game to show, cycle through all three hype
    // animations (DUKE -> DDMF -> GTHC) every 120 seconds, then restore
    // the "No game" message.
    if (g_game.eventId.length() == 0 &&
        nowMs - lastNoGameHypeMs >= NO_GAME_HYPE_INTERVAL_MS) {
      lastNoGameHypeMs = nowMs;
      preGameHype(lcd);
      preGameHype(lcd);
      preGameHype(lcd);
      showNoGame();
    }

    // Periodic OTA update check (hourly).
    if (nowMs - lastUpdateCheckMs >= UPDATE_CHECK_INTERVAL_MS) {
      lastUpdateCheckMs = nowMs;
      checkForUpdate();
    }
  }

  delay(250);
}
