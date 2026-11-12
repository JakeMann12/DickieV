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
#include "wifi_credentials.h"

const char* HOST = "site.api.espn.com";
const char* PATH = "/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?groups=50&limit=400&dates=";
const char* TEAM = "DUKE";
const unsigned long GAME_START_CHECK_INTERVAL_MS = 15UL * 1000UL;
const unsigned long COUNTDOWN_TICK_MS = 1000UL;

// Firmware version (light versioning — bump on each release).
const char* FIRMWARE_VERSION = "1.0.0";

// Button wired from D4 to GND. Uses the internal pullup so the pin reads
// HIGH when open and LOW when the button is pressed.
const uint8_t BUTTON_PIN = D4;

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

// Button debounce state.
static unsigned long lastButtonCheckMs = 0;
static bool lastButtonState = HIGH;

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

// Non-blocking WiFi connection.
// beginWifiConnect() picks the preferred-first network order and starts the
// first attempt. pollWifiConnect() must be called repeatedly; it returns true
// once connected, false while still trying or once every network has been tried.
// The ESP8266 WiFi stack keeps attempting the connection in the background
// (during delay()/yield()), so the caller is free to keep the LCD animated.
static size_t g_wifiOrder[8];
static size_t g_wifiOrderCount = 0;
static size_t g_wifiIndex = 0;
static unsigned long g_wifiStartMs = 0;
static bool g_wifiActive = false;
static bool g_wifiExhausted = false;

static void attemptWifiNetwork(size_t idx) {
  const WifiNetwork& network = wifiNetworks[g_wifiOrder[idx]];
  Serial.print("Trying: ");
  Serial.println(network.ssid);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(network.ssid, network.password);
  g_wifiStartMs = millis();
}

static void beginWifiConnect() {
  const size_t totalNetworks = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);
  g_wifiOrderCount = 0;
  g_wifiActive = false;
  g_wifiExhausted = false;
  if (totalNetworks == 0) {
    return;
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

  if (millis() - g_wifiStartMs >= 10000UL) {
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

// Check GitHub releases for a newer firmware binary and OTA-update if found.
// Based on the project's OTA inspiration snippet.
bool checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("OTA check skipped: not connected to WiFi");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // tradeoff: accept self-signed / no cert validation

  HTTPClient http;
  String url = "https://api.github.com/repos/" + githubOwner + "/" + githubRepo + "/releases/latest";
  http.begin(client, url);
  http.addHeader("User-Agent", "DickieV-ESP8266");
  http.addHeader("Accept", "application/vnd.github+json");

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("GitHub API failed: %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("JSON parse failed");
    return false;
  }

  String latestTag = doc["tag_name"] | "";
  String assetUrl = doc["assets"][0]["browser_download_url"] | "";

  if (latestTag.length() == 0 || assetUrl.length() == 0) {
    Serial.println("No release asset found");
    return false;
  }

  Serial.printf("Current: %s | Latest: %s\n", FIRMWARE_VERSION, latestTag.c_str());

  if (!isNewerVersion(latestTag, FIRMWARE_VERSION)) {
    showOnLcd("Up to date", FIRMWARE_VERSION);
    delay(1200);
    Serial.println("No update needed");
    return false;
  }

  // A newer version exists — show what we're moving to, then flash it.
  showOnLcd("Update", latestTag);
  delay(1200);
  showOnLcd("loading...", "flashing...");
  Serial.println("Starting OTA...");
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, assetUrl, FIRMWARE_VERSION);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA failed: %s\n", ESPhttpUpdate.getLastErrorString().c_str());
      return false;
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

  int status = lcd.begin(16, 2);
  if (status) {
    hd44780::fatalError(status);
  }
  initDunkSprites(lcd);

  readBootCache();

  flashDunkAnimation(lcd);
  flashThreeAnimation(lcd);

  // Brief version splash so you can see which firmware is running.
  showOnLcd("DickieV", FIRMWARE_VERSION);
  delay(1000);

  // Kick off the WiFi connection attempt in the background. The startup hype
  // animation keeps running on the LCD while pollWifiConnect() drives the
  // connection attempt under the hood between animation frames.
  beginWifiConnect();
  bool connected = false;
  if (g_wifiActive) {
    unsigned long connectDeadline = millis() + 40000UL;
    while (!connected && !g_wifiExhausted && millis() < connectDeadline) {
      preGameHype(lcd);          // startup animation keeps displaying
      connected = pollWifiConnect();  // non-blocking; WiFi connects under the hood
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
  unsigned long nowMs = millis();

  // Debounced button read on D4 (INPUT_PULLUP, button to GND -> pressed = LOW).
  if (nowMs - lastButtonCheckMs >= 50UL) {
    lastButtonCheckMs = nowMs;
    bool buttonState = digitalRead(BUTTON_PIN);
    if (buttonState == LOW && lastButtonState == HIGH) {
      // Button only does something when we're connected to WiFi.
      if (WiFi.status() == WL_CONNECTED) {
        showOnLcd("Checking...", "for update");
        checkForUpdate();
      }
    }
    lastButtonState = buttonState;
  }

  if (WiFi.status() == WL_CONNECTED) {
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

    // Periodic OTA update check (hourly).
    if (nowMs - lastUpdateCheckMs >= UPDATE_CHECK_INTERVAL_MS) {
      lastUpdateCheckMs = nowMs;
      checkForUpdate();
    }
  }

  delay(250);
}