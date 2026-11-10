#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>
#include "phrases.h"
#include "animations.h"

struct WifiNetwork {
  const char* ssid;
  const char* password;
};

const WifiNetwork wifiNetworks[] = {
  {"Guest-Monarch", "M0n@rchQu@ntum!"},
  {"SpectrumSetup-87", "inventremedy901"},
  {"Jake", "JakeRocks"},
};

const char* HOST = "site.api.espn.com";
const char* PATH = "/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?groups=50&limit=400";
const char* TEAM = "DUKE";

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

void fetchScore() {
  WiFiClientSecure client;
  client.setInsecure();            // skip cert validation: quick and dirty
  client.setBufferSizes(1024, 512);

  if (!client.connect(HOST, 443)) { showOnLcd("Connect fail", ""); return; }

  // HTTP/1.0 so the server doesn't send chunked encoding
  client.print(String("GET ") + PATH + " HTTP/1.0\r\nHost: " + HOST +
               "\r\nConnection: close\r\n\r\n");
  if (!client.find("\r\n\r\n")) { showOnLcd("Bad response", ""); return; }

  JsonDocument filter;
  JsonObject f = filter["events"][0].to<JsonObject>();
  f["status"]["type"]["shortDetail"] = true;
  f["competitions"][0]["competitors"][0]["score"] = true;
  f["competitions"][0]["competitors"][0]["team"]["abbreviation"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
    doc,
    client,
    DeserializationOption::Filter(filter),
    DeserializationOption::NestingLimit(32));
  if (err) { showOnLcd("JSON err", err.c_str()); return; }

  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    JsonArray comps = ev["competitions"][0]["competitors"];
    bool hasTeam = false;
    for (JsonObject c : comps)
      if (String(c["team"]["abbreviation"] | "") == TEAM) hasTeam = true;
    if (!hasTeam) continue;

    String line1 = "";
    for (JsonObject c : comps) {
      if (line1.length()) line1 += " ";
      line1 += String(c["team"]["abbreviation"] | "?") + " " + String(c["score"] | "0");
    }
    showOnLcd(line1, ev["status"]["type"]["shortDetail"] | "");
    return;
  }
  showOnLcd("No Duke game", "today");
}

void setup() {
  Serial.begin(115200);

  int status = lcd.begin(16, 2);
  if (status) {
    hd44780::fatalError(status);   // blinks onboard LED if init fails
  }
  initDunkSprites(lcd);
  lcd.print("LCD OK");
  delay(1500);

  // temporary test: remove once you are happy with the animation
  triggerDunkCelebration(lcd, "Boozer");
  delay(1000);
  triggerThreeCelebration(lcd, "Boozer");

  lcd.clear();
  lcd.print("Connecting...");
  WiFi.mode(WIFI_STA);

  bool connected = false;
  for (const auto& network : wifiNetworks) {
    Serial.print("Trying: ");
    Serial.println(network.ssid);
    WiFi.begin(network.ssid, network.password);

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
      delay(300);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      Serial.print("Connected to ");
      Serial.println(network.ssid);
      break;
    }
  }

  if (!connected) {
    showOnLcd("WiFi failed", "check networks");
    return;
  }
  showOnLcd("WiFi up", WiFi.localIP().toString());
  delay(1500);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) fetchScore();
  delay(15000);
}