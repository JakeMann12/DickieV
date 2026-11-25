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
const char* PATH = "/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?groups=50&limit=400&dates=";
// Full-season Duke schedule (includes today's game). One small request instead
// of scanning a ~1MB all-D1 scoreboard per day, which often failed on-device.
const char* SCHEDULE_PATH = "/apis/site/v2/sports/basketball/mens-college-basketball/teams/duke/schedule";
const char* TEAM = "DUKE";
const unsigned long GAME_START_CHECK_INTERVAL_MS = 30UL * 1000UL;
// How often a cached future game is re-validated against the schedule.
const unsigned long SCHEDULE_RECHECK_INTERVAL_MS = 60UL * 60UL * 1000UL;
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
  char lastDateKey[9];
  char lastConnectedSsid[32];
  char lastGameStartIso[32];
  char lastOpponent[12];
};

// Bump when BootCache's layout changes so stale EEPROM data is ignored.
static const uint32_t BOOT_CACHE_MAGIC = 0x44524457u;
static String cachedGameStartIso;
// Opponent label for the cached game, e.g. "vs UNC" (home) or "@ UVA" (away).
static String cachedOpponent;
static unsigned long lastGameStartCheckMs = 0;
static unsigned long lastScheduleCheckMs = 0;
static unsigned long lastCountdownRefreshMs = 0;
static unsigned long lastNoGameHypeMs = 0;
static BootCache bootCache = {};

// How often to cycle the hype animations when there's no Duke game today.
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
  if (diff < 0) return "LIVE";

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
static String scanDukeGamesForDate(const String& dateKey, String& scoreLine1, String& scoreDetail, bool& foundLiveOrFinal, String& opponent);
static bool findNextDukeGame(String& gameStartIso, String& opponent);

// "vs UNC" / "@ UVA", with the ESPN abbreviation trimmed only as far as needed
// for matchupLine() to fit on one 16-char LCD row.
static String opponentLabel(const String& abbr, bool away) {
  if (abbr.length() == 0) return "";
  String prefix = away ? "@ " : "vs ";
  int room = 16 - 5 - (int)prefix.length();  // 5 = "DUKE "
  return prefix + abbr.substring(0, room);
}

static String matchupLine(const String& opponent) {
  return opponent.length() > 0 ? "DUKE " + opponent : String("DUKE game");
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
  return bootCache.magic == BOOT_CACHE_MAGIC;
}

static void writeBootCache(const String& dateKey, const String& ssid, const String& gameStartIso) {
  memset(&bootCache, 0, sizeof(bootCache));
  bootCache.magic = BOOT_CACHE_MAGIC;
  strlcpy(bootCache.lastDateKey, dateKey.c_str(), sizeof(bootCache.lastDateKey));
  strlcpy(bootCache.lastConnectedSsid, ssid.c_str(), sizeof(bootCache.lastConnectedSsid));
  strlcpy(bootCache.lastGameStartIso, gameStartIso.c_str(), sizeof(bootCache.lastGameStartIso));
  if (gameStartIso.length() > 0) {
    strlcpy(bootCache.lastOpponent, cachedOpponent.c_str(), sizeof(bootCache.lastOpponent));
  }
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
    writeBootCache(makePstDateString(), connectedSsid, cachedGameStartIso);
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

static void refreshCountdownDisplay() {
  if (cachedGameStartIso.length() == 0) return;

  String line1 = matchupLine(cachedOpponent);
  String line2 = formatCountdown(cachedGameStartIso);

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
// Called from loop() and while a slow ESPN download is being parsed.
static void tickCountdown() {
  if (cachedGameStartIso.length() == 0) return;
  unsigned long nowMs = millis();
  if (nowMs - lastCountdownRefreshMs < COUNTDOWN_TICK_MS) return;
  lastCountdownRefreshMs = nowMs;
  refreshCountdownDisplay();
}

// Stream wrapper that keeps the countdown ticking while ArduinoJson reads a
// large HTTPS response, which can otherwise block loop() for many seconds.
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

void fetchScore() {
  String todayKey = makePstDateString();
  String scoreLine1, scoreDetail;
  bool foundLiveOrFinal = false;
  String upcomingOpponent;
  String upcomingIso = scanDukeGamesForDate(todayKey, scoreLine1, scoreDetail, foundLiveOrFinal, upcomingOpponent);

  Serial.printf("requestDate=%s\n", todayKey.c_str());

  // An upcoming Duke game today takes priority over any cached future game.
  if (upcomingIso.length() > 0) {
    cachedGameStartIso = upcomingIso;
    cachedOpponent = upcomingOpponent;
    writeBootCache(todayKey, WiFi.SSID(), cachedGameStartIso);
    long secs = secondsUntil(upcomingIso);
    if (secs > 0 && secs <= 120) {
      preGameHype(lcd);
    }
    showOnLcd(matchupLine(cachedOpponent), formatCountdown(upcomingIso));
    return;
  }

  // A Duke game today that is live or final — show the score.
  if (foundLiveOrFinal) {
    writeBootCache(todayKey, WiFi.SSID(), "");
    cachedGameStartIso = "";
    showOnLcd(scoreLine1, scoreDetail);
    return;
  }

  // Nothing from today's scoreboard (or that fetch failed). Drop a cached game
  // whose start time has passed, then consult the team schedule when nothing
  // is cached or the cache is due for re-validation.
  if (cachedGameStartIso.length() > 0 && secondsUntil(cachedGameStartIso) < 0) {
    cachedGameStartIso = "";
  }

  bool scheduleDue = lastScheduleCheckMs == 0 ||
                     millis() - lastScheduleCheckMs >= SCHEDULE_RECHECK_INTERVAL_MS;
  if (cachedGameStartIso.length() > 0 && !scheduleDue) {
    refreshCountdownDisplay();
    return;
  }

  String nextIso, nextOpponent;
  if (!findNextDukeGame(nextIso, nextOpponent)) {
    // Fetch failed: keep whatever we had rather than guessing.
    if (cachedGameStartIso.length() > 0) {
      refreshCountdownDisplay();
      return;
    }
    showOnLcd("No Duke game", String("today v") + String(FIRMWARE_VERSION));
    return;
  }
  lastScheduleCheckMs = millis();

  if (nextIso.length() > 0) {
    cachedGameStartIso = nextIso;
    cachedOpponent = nextOpponent;
    writeBootCache(todayKey, WiFi.SSID(), cachedGameStartIso);
    refreshCountdownDisplay();
    return;
  }

  cachedGameStartIso = "";
  writeBootCache(todayKey, WiFi.SSID(), "");
  showOnLcd("No Duke game", String("today v") + String(FIRMWARE_VERSION));
}


static String scanDukeGamesForDate(const String& dateKey,
                                   String& scoreLine1,
                                   String& scoreDetail,
                                   bool& foundLiveOrFinal,
                                   String& opponent) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(1024, 512);

  scoreLine1 = "";
  scoreDetail = "";
  foundLiveOrFinal = false;
  opponent = "";

  if (!client.connect(HOST, 443)) {
    Serial.println("Connect fail (scan)");
    return "";
  }

  String requestPath = String(PATH) + dateKey;
  client.print(String("GET ") + requestPath + " HTTP/1.0\r\nHost: " + HOST +
               "\r\nConnection: close\r\n\r\n");
  if (!client.find("\r\n\r\n")) {
    Serial.println("Bad response (scan)");
    client.stop();
    return "";
  }

  JsonDocument filter;
  JsonObject f = filter["events"][0].to<JsonObject>();
  f["status"]["type"]["state"] = true;
  f["status"]["type"]["shortDetail"] = true;
  f["competitions"][0]["startDate"] = true;
  f["competitions"][0]["neutralSite"] = true;
  JsonArray comps = f["competitions"][0]["competitors"].to<JsonArray>();
  JsonObject comp0 = comps.add<JsonObject>();
  comp0["score"] = true;
  comp0["homeAway"] = true;
  comp0["team"]["abbreviation"] = true;
  JsonObject comp1 = comps.add<JsonObject>();
  comp1["score"] = true;
  comp1["homeAway"] = true;
  comp1["team"]["abbreviation"] = true;

  JsonDocument doc;
  CountdownTickingStream tickingClient(client);
  DeserializationError err = deserializeJson(
    doc,
    tickingClient,
    DeserializationOption::Filter(filter),
    DeserializationOption::NestingLimit(32));
  client.stop();
  if (err) {
    Serial.printf("JSON err (scan): %s\n", err.c_str());
    return "";
  }

  String upcomingIso = "";

  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    JsonArray evComps = ev["competitions"][0]["competitors"];
    String gameStart = ev["competitions"][0]["startDate"] | "";

    bool hasDuke = false;
    for (JsonObject c : evComps) {
      if (String(c["team"]["abbreviation"] | "") == TEAM) {
        hasDuke = true;
        break;
      }
    }
    if (!hasDuke) {
      continue;
    }

    String status = ev["status"]["type"]["state"] | "";
    Serial.printf("dukeEvent date=%s state=%s start=%s\n", dateKey.c_str(), status.c_str(), gameStart.c_str());

    if (status == "pre" || status == "scheduled") {
      if (upcomingIso.length() == 0) {
        upcomingIso = gameStart;
        bool dukeAway = false;
        String oppAbbr = "";
        for (JsonObject c : evComps) {
          String abbr = c["team"]["abbreviation"] | "";
          if (abbr == TEAM) {
            dukeAway = String(c["homeAway"] | "") == "away";
          } else {
            oppAbbr = abbr;
          }
        }
        bool neutral = ev["competitions"][0]["neutralSite"] | false;
        opponent = opponentLabel(oppAbbr, dukeAway && !neutral);
      }
      continue;
    }

    // Live or final Duke game today — capture score summary (first one).
    if (!foundLiveOrFinal) {
      foundLiveOrFinal = true;
      String line1 = "";
      for (JsonObject c : evComps) {
        if (line1.length()) {
          line1 += " ";
        }
        line1 += String(c["team"]["abbreviation"] | "?") + " " + String(c["score"] | "0");
      }
      scoreLine1 = line1;
      scoreDetail = ev["status"]["type"]["shortDetail"] | "";
    }
  }

  return upcomingIso;
}

// Returns false on network/parse failure. On success, gameStartIso is the
// earliest not-yet-started Duke game (empty if none remain this season).
static bool findNextDukeGame(String& gameStartIso, String& opponent) {
  gameStartIso = "";
  opponent = "";

  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(1024, 512);

  if (!client.connect(HOST, 443)) {
    Serial.println("Connect fail (schedule)");
    return false;
  }

  client.print(String("GET ") + SCHEDULE_PATH + " HTTP/1.0\r\nHost: " + HOST +
               "\r\nConnection: close\r\n\r\n");
  if (!client.find("\r\n\r\n")) {
    Serial.println("Bad response (schedule)");
    client.stop();
    return false;
  }

  JsonDocument filter;
  JsonObject f = filter["events"][0].to<JsonObject>();
  f["date"] = true;
  JsonObject fc = f["competitions"][0].to<JsonObject>();
  fc["neutralSite"] = true;
  fc["status"]["type"]["state"] = true;
  JsonObject fcomp = fc["competitors"][0].to<JsonObject>();
  fcomp["homeAway"] = true;
  fcomp["team"]["abbreviation"] = true;

  JsonDocument doc;
  CountdownTickingStream tickingClient(client);
  DeserializationError err = deserializeJson(
    doc,
    tickingClient,
    DeserializationOption::Filter(filter),
    DeserializationOption::NestingLimit(32));
  client.stop();
  if (err) {
    Serial.printf("JSON err (schedule): %s\n", err.c_str());
    return false;
  }

  time_t bestStart = 0;
  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    JsonObject comp = ev["competitions"][0];
    String state = comp["status"]["type"]["state"] | "";
    if (state != "pre") {
      continue;
    }
    String iso = ev["date"] | "";
    time_t start = parseIsoUtc(iso);
    if (start == 0 || (bestStart != 0 && start >= bestStart)) {
      continue;
    }

    bool dukeAway = false;
    String oppAbbr = "";
    for (JsonObject c : comp["competitors"].as<JsonArray>()) {
      String abbr = c["team"]["abbreviation"] | "";
      if (abbr == TEAM) {
        dukeAway = String(c["homeAway"] | "") == "away";
      } else {
        oppAbbr = abbr;
      }
    }
    bool neutral = comp["neutralSite"] | false;

    bestStart = start;
    gameStartIso = iso;
    opponent = opponentLabel(oppAbbr, dukeAway && !neutral);
  }

  Serial.printf("nextDukeGame start=%s opp=%s\n", gameStartIso.c_str(), opponent.c_str());
  return true;
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

  String todayKey = makePstDateString();
  if (bootCache.lastDateKey[0] != '\0' && String(bootCache.lastDateKey) == todayKey &&
      bootCache.lastGameStartIso[0] != '\0') {
    cachedGameStartIso = String(bootCache.lastGameStartIso);
    cachedOpponent = String(bootCache.lastOpponent);
    refreshCountdownDisplay();
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
    if (lastGameStartCheckMs == 0 ||
        nowMs - lastGameStartCheckMs >= GAME_START_CHECK_INTERVAL_MS) {
      lastGameStartCheckMs = nowMs;
      fetchScore();
    }

    tickCountdown();

    // When there's no Duke game today, cycle through all three hype
    // animations (DUKE -> DDMF -> GTHC) every 120 seconds, then restore
    // the "No Duke game" message.
    if (cachedGameStartIso.length() == 0 &&
        nowMs - lastNoGameHypeMs >= NO_GAME_HYPE_INTERVAL_MS) {
      lastNoGameHypeMs = nowMs;
      preGameHype(lcd);
      preGameHype(lcd);
      preGameHype(lcd);
      showOnLcd("No Duke game", String("today v") + String(FIRMWARE_VERSION));
    }

    // Periodic OTA update check (hourly).
    if (nowMs - lastUpdateCheckMs >= UPDATE_CHECK_INTERVAL_MS) {
      lastUpdateCheckMs = nowMs;
      checkForUpdate();
    }
  }

  delay(250);
}