#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <time.h>

// WiFi debugging: set to 1 to skip boot animations, scan and list visible
// networks, and show each SSID/password attempt plus the failure reason on the LCD.
// Can also be set from platformio.ini with build_flags = -DWIFI_DEBUG_LCD=1.
// Set it back to 0 before committing.
#ifndef WIFI_DEBUG_LCD
#define WIFI_DEBUG_LCD 0
#endif

// Defined in main.cpp.
void showOnLcd(const String& l1, const String& l2);

// Every hook below is a no-op when WIFI_DEBUG_LCD is 0, so main.cpp can call
// them unconditionally.
namespace wifiDebug {

// Non-ASCII bytes (e.g. the curly apostrophe in "Jake’s iPhone") render as '~'
// so they're visible on the LCD.
inline String lcdVisible(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    out += (c >= 32 && c < 127) ? c : '~';
  }
  return out;
}

// Scan and show every visible network, then check each configured SSID
// against the scan results. Network only needs a String `ssid` member.
template <typename Network>
void scanAndCheck(const Network* networks, size_t networkCount) {
  showOnLcd("Scanning WiFi...", "");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks(false, true);  // include hidden networks
  if (n < 0) {
    showOnLcd("ERR: scan failed", String(n));
    delay(4000);
    return;
  }
  showOnLcd("Found " + String(n), "networks (2.4G)");
  delay(2500);

  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    String name = ssid.length() ? lcdVisible(ssid) : String("<hidden>");
    showOnLcd(String(i + 1) + ":" + name,
              "ch" + String(WiFi.channel(i)) + " " + String(WiFi.RSSI(i)) + "dB L" +
                  String(ssid.length()));
    delay(2500);
    if (name.length() + String(i + 1).length() + 1 > 16) {
      showOnLcd(name.substring(16 - String(i + 1).length() - 1), "");
      delay(2000);
    }
  }

  for (size_t c = 0; c < networkCount; ++c) {
    const String& want = networks[c].ssid;
    const char* result = "NOT IN SCAN";
    for (int i = 0; i < n; ++i) {
      String seen = WiFi.SSID(i);
      if (seen == want) { result = "MATCH"; break; }
      String a = seen, b = want;
      a.trim(); b.trim();
      if (a.equalsIgnoreCase(b)) {
        result = "case/space diff";
      } else if (lcdVisible(a).equalsIgnoreCase(lcdVisible(b)) ||
                 a.length() != lcdVisible(a).length()) {
        // Same once special chars are masked, or the scanned name has
        // non-ASCII chars the secret may have typed differently.
        if (lcdVisible(a).length() > 0 &&
            lcdVisible(a).substring(0, 3).equalsIgnoreCase(lcdVisible(b).substring(0, 3))) {
          result = "special char?";
        }
      }
    }
    showOnLcd("Cfg:" + lcdVisible(want), String(result) + " L" + String(want.length()));
    delay(3500);
  }
  WiFi.scanDelete();
}

// Boot diagnostics: show how many networks are in the build, then scan.
// Returns false if there is nothing to connect to (caller should stop).
template <typename Network>
bool bootDiagnostics(const Network* networks, size_t networkCount) {
  if (!WIFI_DEBUG_LCD) return true;
  if (networkCount == 0) {
    showOnLcd("ERR: no WiFi", "creds in build");
    return false;
  }
  showOnLcd(String(networkCount) + " networks", "in build");
  delay(2000);
  scanAndCheck(networks, networkCount);
  return true;
}

// Show the SSID/password about to be tried (attempt idx of total).
inline void showAttempt(size_t idx, size_t total, const String& ssid, const String& password) {
  if (!WIFI_DEBUG_LCD) return;
  showOnLcd(String(idx + 1) + "/" + String(total) + " " + ssid, password);
  delay(2500);
  // Show the full SSID/password if they don't fit on the 16-char lines.
  if (ssid.length() > 12 || password.length() > 16) {
    showOnLcd(ssid.substring(12), password.substring(16));
    delay(2500);
  }
}

inline void showFailure(const String& ssid, wl_status_t status, const char* statusText) {
  if (!WIFI_DEBUG_LCD) return;
  showOnLcd("FAIL " + ssid, String(static_cast<int>(status)) + " " + statusText);
  delay(4000);
}

inline void showConnected() {
  if (!WIFI_DEBUG_LCD) return;
  showOnLcd("OK " + WiFi.SSID(), WiFi.localIP().toString());
  delay(4000);
}

inline void showNtpResult() {
  if (!WIFI_DEBUG_LCD) return;
  if (time(nullptr) < 1700000000) {
    showOnLcd("ERR: NTP time", "sync failed");
    delay(4000);
  }
}

}  // namespace wifiDebug
