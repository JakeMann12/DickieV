#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>
#include <time.h>
#include "phrases.h"
#include "animations.h"
#include "wifi_credentials.h"

const char* HOST = "site.api.espn.com";
const char* PATH = "/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?groups=50&limit=400&dates=";
const char* TEAM = "DUKE";
const unsigned long GAME_START_CHECK_INTERVAL_MS = 15UL * 1000UL;
const unsigned long COUNTDOWN_TICK_MS = 1000UL;

struct BootCache {
  uint32_t magic;
  char lastDateKey[9];
  char lastConnectedSsid[32];
  char lastGameStartIso[32];
};

static const uint32_t BOOT_CACHE_MAGIC = 0x44524456u;
static String cachedGameStartIso;
static unsigned long lastGameStartCheckMs = 0;
static unsigned long lastCountdownRefreshMs = 0;
static BootCache bootCache = {};

hd44780_pinIO lcd(D1, D2, D5, D6, D7, D0);

String safeLcdLine(const String& s) {
  String out = s;
  if (out.length() > 16) {
    out = out.substring(0, 16);
  }
  return out;
}

void showOnLcd(const String& l1, const String& l2) {
  Serial.println(l1);
  Serial.println(l2);
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

static String formatPstTime(const String& iso) {
  time_t startUtc = parseIsoUtc(iso);
  if (startUtc == 0) return "TBD";

  time_t pstEpoch = startUtc - (8 * 3600L);
  struct tm* pst = gmtime(&pstEpoch);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d PST", pst->tm_hour, pst->tm_min);
  return String(buf);
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

static String makePstDateKeyFromUtc(time_t utcEpoch) {
  time_t pstEpoch = utcEpoch - (8 * 3600L);
  struct tm* pst = gmtime(&pstEpoch);

  char yearBuf[5];
  char monthBuf[3];
  char dayBuf[3];
  snprintf(yearBuf, sizeof(yearBuf), "%04d", pst->tm_year + 1900);
  snprintf(monthBuf, sizeof(monthBuf), "%02d", pst->tm_mon + 1);
  snprintf(dayBuf, sizeof(dayBuf), "%02d", pst->tm_mday);

  return String(yearBuf) + String(monthBuf) + String(dayBuf);
}

static bool isDukeGameToday(const String& iso) {
  time_t startUtc = parseIsoUtc(iso);
  if (startUtc == 0) return false;

  return makePstDateKeyFromUtc(startUtc) == makePstDateString();
}

static bool isDukeEventForToday(const JsonObject& ev) {
  JsonArray comps = ev["competitions"][0]["competitors"];
  String gameStart = ev["competitions"][0]["startDate"] | "";

  for (JsonObject c : comps) {
    if (String(c["team"]["abbreviation"] | "") == TEAM) {
      return isDukeGameToday(gameStart);
    }
  }

  return false;
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
  snprintf(bootCache.lastDateKey, sizeof(bootCache.lastDateKey), "%s", dateKey.c_str());
  snprintf(bootCache.lastConnectedSsid, sizeof(bootCache.lastConnectedSsid), "%s", ssid.c_str());
  snprintf(bootCache.lastGameStartIso, sizeof(bootCache.lastGameStartIso), "%s", gameStartIso.c_str());
  EEPROM.begin(sizeof(BootCache));
  EEPROM.put(0, bootCache);
  EEPROM.commit();
  EEPROM.end();
}

static bool tryConnectToPreferredWifi() {
  const size_t totalNetworks = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);
  if (totalNetworks == 0) {
    return false;
  }

  int preferredIndex = -1;
  if (bootCache.lastConnectedSsid[0] != '\0') {
    for (size_t i = 0; i < totalNetworks; ++i) {
      if (String(wifiNetworks[i].ssid) == String(bootCache.lastConnectedSsid)) {
        preferredIndex = static_cast<int>(i);
        break;
      }
    }
  }

  size_t ordered[8] = {0};
  size_t orderCount = 0;
  if (preferredIndex >= 0) {
    ordered[orderCount++] = static_cast<size_t>(preferredIndex);
  }
  for (size_t i = 0; i < totalNetworks; ++i) {
    if (static_cast<int>(i) != preferredIndex) {
      ordered[orderCount++] = i;
    }
  }

  for (size_t orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
    const WifiNetwork& network = wifiNetworks[ordered[orderIndex]];
    Serial.print("Trying: ");
    Serial.println(network.ssid);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(network.ssid, network.password);

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 10000UL) {
      delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
      String connectedSsid = WiFi.SSID();
      Serial.print("Connected to ");
      Serial.println(network.ssid);
      writeBootCache(makePstDateString(), connectedSsid, cachedGameStartIso);
      return true;
    }
  }

  return false;
}

static void refreshCountdownDisplay() {
  if (cachedGameStartIso.length() == 0) return;

  String line1 = "DUKE @ " + formatPstTime(cachedGameStartIso);
  String line2 = formatCountdown(cachedGameStartIso);
  showOnLcd(line1, line2);
}

void fetchScore() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(1024, 512);

  if (!client.connect(HOST, 443)) {
    showOnLcd("Connect fail", "");
    return;
  }

  String requestPath = String(PATH) + makePstDateString();
  client.print(String("GET ") + requestPath + " HTTP/1.0\r\nHost: " + HOST +
               "\r\nConnection: close\r\n\r\n");
  if (!client.find("\r\n\r\n")) {
    showOnLcd("Bad response", "");
    return;
  }

  JsonDocument filter;
  JsonObject f = filter["events"][0].to<JsonObject>();
  f["status"]["type"]["state"] = true;
  f["status"]["type"]["shortDetail"] = true;
  f["competitions"][0]["startDate"] = true;
  JsonArray comps = f["competitions"][0]["competitors"].to<JsonArray>();
  JsonObject comp0 = comps.add<JsonObject>();
  comp0["score"] = true;
  comp0["team"]["abbreviation"] = true;
  JsonObject comp1 = comps.add<JsonObject>();
  comp1["score"] = true;
  comp1["team"]["abbreviation"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
    doc,
    client,
    DeserializationOption::Filter(filter),
    DeserializationOption::NestingLimit(32));
  if (err) {
    showOnLcd("JSON err", err.c_str());
    return;
  }

  Serial.printf("requestDate=%s\n", makePstDateString().c_str());

  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    JsonArray comps = ev["competitions"][0]["competitors"];
    String gameStart = ev["competitions"][0]["startDate"] | "";

    bool hasDuke = false;
    for (JsonObject c : comps) {
      if (String(c["team"]["abbreviation"] | "") == TEAM) {
        hasDuke = true;
      }
    }
    if (!hasDuke) {
      continue;
    }

    String status = ev["status"]["type"]["state"] | "";
    Serial.printf("dukeEvent state=%s start=%s\n", status.c_str(), gameStart.c_str());
    if (status == "pre" || status == "scheduled") {
      cachedGameStartIso = gameStart;
      writeBootCache(makePstDateString(), WiFi.SSID(), cachedGameStartIso);
      long secs = secondsUntil(gameStart);
      if (secs > 0 && secs <= 120) {
        preGameHype(lcd);
      }
      String line1 = "DUKE @ " + formatPstTime(gameStart);
      String line2 = formatCountdown(gameStart);
      showOnLcd(line1, line2);
      return;
    }

    cachedGameStartIso = "";
    writeBootCache(makePstDateString(), WiFi.SSID(), cachedGameStartIso);

    String line1 = "";
    for (JsonObject c : comps) {
      if (line1.length()) {
        line1 += " ";
      }
      line1 += String(c["team"]["abbreviation"] | "?") + " " + String(c["score"] | "0");
    }
    showOnLcd(line1, ev["status"]["type"]["shortDetail"] | "");
    return;
  }

  cachedGameStartIso = "";
  writeBootCache(makePstDateString(), WiFi.SSID(), cachedGameStartIso);
  showOnLcd("No Duke game", "today");
}

void setup() {
  Serial.begin(115200);

  int status = lcd.begin(16, 2);
  if (status) {
    hd44780::fatalError(status);
  }
  initDunkSprites(lcd);

  readBootCache();

  flashDunkAnimation(lcd);
  flashThreeAnimation(lcd);

  bool connected = false;
  for (uint8_t i = 0; i < 2 && !connected; ++i) {
    preGameHype(lcd);
    connected = tryConnectToPreferredWifi();
  }

  if (!connected) {
    lcd.clear();
    lcd.print("Connecting...");
    for (uint8_t i = 0; i < 2 && !connected; ++i) {
      preGameHype(lcd);
      connected = tryConnectToPreferredWifi();
    }
  }

  if (connected) {
    showOnLcd("WiFi connected", WiFi.localIP().toString());
    delay(1000);
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

  String todayKey = makePstDateString();
  if (bootCache.lastDateKey[0] != '\0' && String(bootCache.lastDateKey) == todayKey &&
      bootCache.lastGameStartIso[0] != '\0') {
    cachedGameStartIso = String(bootCache.lastGameStartIso);
    refreshCountdownDisplay();
  } else {
    fetchScore();
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long nowMs = millis();

    if (lastGameStartCheckMs == 0 ||
        nowMs - lastGameStartCheckMs >= GAME_START_CHECK_INTERVAL_MS) {
      lastGameStartCheckMs = nowMs;
      fetchScore();
    }

    if (cachedGameStartIso.length() > 0 &&
        nowMs - lastCountdownRefreshMs >= COUNTDOWN_TICK_MS) {
      lastCountdownRefreshMs = nowMs;
      refreshCountdownDisplay();
    }
  }

  delay(250);
}