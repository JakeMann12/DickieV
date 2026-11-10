#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>

const char* WIFI_SSID = "SpectrumSetup-87";//"Jake";
const char* WIFI_PASS = "inventremedy901";//"JakeRocks";     //"M0n@rchQu@ntum!";
const char* HOST = "site.api.espn.com";
const char* PATH = "/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?groups=50&limit=400";
const char* TEAM = "DUKE";   // ESPN team abbreviation

// Wiring map for a 16x2 LCD (parallel):
// RS=D1(GPIO5), E=D2(GPIO4), D4=D5(GPIO14), D5=D6(GPIO12), D6=D7(GPIO13), D7=D0(GPIO16)
hd44780_pinIO lcd(D1, D2, D5, D6, D7, D0);

void showOnLcd(const String& l1, const String& l2) {
  Serial.println(l1);
  Serial.println(l2);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);
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
  DeserializationError err = deserializeJson(doc, client, DeserializationOption::Filter(filter));
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
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println("\nWiFi up");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi up");
  lcd.print("LAL 87  BOS 84");
  lcd.setCursor(0, 1);
  lcd.print("Q3  4:12");
  delay(1555000);
}

void loop() {
  fetchScore();
  delay(15000);   // poll every 15s
}