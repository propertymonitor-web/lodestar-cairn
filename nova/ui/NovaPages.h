// NovaPages - the LODESTAR NOVA screens for the 128x64 OLED.
// By Wolf Reactive Systems (WRS).
//
// Plain drawing functions: they read from nova::Nova and draw on a DisplayDriver, nothing else.
// tools/apply_nova.py copies this file into MeshCore's companion UI and calls it from the home
// screen. Text is size 1 (6x8 pixels), so a row holds 21 characters; the title bar and page
// dots use the top 18 pixels, so there is room for 5 rows below them.
#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/Nova.h>
#include <stdio.h>
#include <math.h>

#ifndef NOVA_PRESS
  #ifdef PRESS_LABEL
    #define NOVA_PRESS PRESS_LABEL
  #else
    #define NOVA_PRESS "press Enter"
  #endif
#endif

// What the "Enter" action is called on this board: a single-button board uses a long press.
#if UI_HAS_JOYSTICK || UI_HAS_ROTARY_INPUT
  #define NOVA_HOLD "Enter"
#else
  #define NOVA_HOLD "hold"
#endif

namespace novaui {

static const int ROW0 = 19;      // y of the first row
static const int ROWH = 9;       // row pitch

static inline void row(DisplayDriver& d, int n, const char* s) {
  char b[24]; snprintf(b, sizeof(b), "%.21s", s);
  d.setCursor(0, ROW0 + n * ROWH);
  d.print(b);
}

static inline void uplinkId(const lodestar::Uplink& u, char* out, size_t cap) {
  if (u.key_len >= 3) snprintf(out, cap, "%02X%02X%02X", u.key[0], u.key[1], u.key[2]);
  else if (u.key_len == 2) snprintf(out, cap, "%02X%02X", u.key[0], u.key[1]);
  else snprintf(out, cap, "%02X", u.key[0]);
}

// ---- LINK: the repeaters this companion hears, best first ---------------------------------
static inline void pageLink(DisplayDriver& d, nova::Nova& N, uint32_t now) {
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  char b[48], id[8];
  int idx[3]; int n = N.ranked(idx, 3, now);
  int all = N.link.count();
  if (N.link.best() >= 0) {
    uplinkId(N.link.entry(N.link.best()), id, sizeof(id));
    int t = N.scoreTrend();
    snprintf(b, sizeof(b), "UPLINK %s sc%d %s", id, nova::Nova::pc(N.link.bestScore()), t > 0 ? "up" : (t < 0 ? "down" : "steady"));
  } else if (all > 0) {
    snprintf(b, sizeof(b), "NO USABLE UPLINK");
  } else {
    snprintf(b, sizeof(b), "NOTHING HEARD YET");
  }
  row(d, 0, b);
  for (int i = 0; i < 3; i++) {
    if (i >= n) { row(d, 1 + i, ""); continue; }
    const lodestar::Uplink& u = N.link.entry(idx[i]);
    uplinkId(u, id, sizeof(id));
    snprintf(b, sizeof(b), "%c%-6s%3d%+4ddB%4lus", idx[i] == N.link.best() ? '*' : ' ', id, nova::Nova::pc(N.link.scoreOf(idx[i], now)),
             (int)lroundf(u.snr), (unsigned long)(nova::elapsed(now, u.last_ms) / 1000u));
    row(d, 1 + i, b);
  }
  snprintf(b, sizeof(b), "heard %d  rx %lu", all, (unsigned long)N.diag.rx);
  row(d, 4, b);
}

// ---- SENT: what happened to the messages you sent -----------------------------------------
static inline void pageSent(DisplayDriver& d, nova::Nova& N, uint32_t now) {
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  char b[48];
  const nova::DeliveryStats& s = N.deliveries.stats;
  snprintf(b, sizeof(b), "SENT %lu ok %lu fail %lu", (unsigned long)s.sent, (unsigned long)s.delivered, (unsigned long)s.failed);
  row(d, 0, b);
  if (N.deliveries.count() == 0) { row(d, 1, "no messages sent yet"); row(d, 2, "send one from the app"); return; }
  for (int i = 0; i < 4; i++) {
    const nova::Delivery* dl = N.deliveries.newest(i);
    if (!dl) break;
    nova::DeliveryLog::line(*dl, now, b, sizeof(b));
    row(d, 1 + i, b);
  }
}

// ---- RANGE: ask nearby repeaters to answer, and judge the result -----------------------------
struct RangeData {
  bool     available;     // false when this build has no repeater-discovery screen
  bool     idle;          // no check has been run yet
  bool     running;       // a request went out and the answers are still coming in
  int      count;
  float    best_in;       // strongest reply as we heard it (dB)
  float    best_out;      // how that repeater heard us (dB)
  float    worst_in;
  uint8_t  sf;
};

static inline void pageRange(DisplayDriver& d, nova::Nova& N, const RangeData& r) {
  (void)N;
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  char b[48];
  row(d, 0, "RANGE CHECK");
  if (!r.available) { row(d, 1, "not in this build"); return; }
  if (r.running) { row(d, 1, "listening..."); if (r.count > 0) { snprintf(b, sizeof(b), "%d answered so far", r.count); row(d, 2, b); } return; }
  if (r.idle) { row(d, 1, "test your reach to"); row(d, 2, "nearby repeaters"); row(d, 3, "start: " NOVA_PRESS); return; }
  uint8_t v = nova::rangeVerdict(r.count, r.best_in, r.sf);
  snprintf(b, sizeof(b), "%s: %d repeater%s", nova::rangeName(v), r.count, r.count == 1 ? "" : "s");
  row(d, 1, b);
  if (r.count > 0) {
    snprintf(b, sizeof(b), "best in %+.1f out %+.1f", (double)r.best_in, (double)r.best_out);
    row(d, 2, b);
    snprintf(b, sizeof(b), "weakest in %+.1f dB", (double)r.worst_in);
    row(d, 3, b);
  } else {
    row(d, 2, "nobody answered");
    row(d, 3, "move to higher ground");
  }
  row(d, 4, "again: " NOVA_PRESS);
}

// ---- TRIP ------------------------------------------------------------------------------------
static inline void pageTrip(DisplayDriver& d, nova::Nova& N, uint32_t now, bool gps_on, bool armed) {
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  char b[48];
  const nova::Trip& t = N.trip;
  if (t.dist_m < 1000.0) snprintf(b, sizeof(b), "TRIP %d m", (int)(t.dist_m + 0.5)); else snprintf(b, sizeof(b), "TRIP %.1f km", t.dist_m / 1000.0);
  row(d, 0, b);
  unsigned long tot = t.total_ms / 1000u, mov = t.moving_ms / 1000u;
  snprintf(b, sizeof(b), "time %lu:%02lu mov %lu:%02lu", tot / 3600u, (tot / 60u) % 60u, mov / 3600u, (mov / 60u) % 60u);
  row(d, 1, b);
  double avg = t.moving_ms > 60000u ? (t.dist_m / ((double)t.moving_ms / 1000.0)) * (double)NOVA_SPD_MUL : 0.0;
  snprintf(b, sizeof(b), "top %d avg %d " NOVA_SPD_UNIT, (int)(t.top_mps * NOVA_SPD_MUL + 0.5f), (int)(avg + 0.5));
  row(d, 2, b);
  if (!gps_on) row(d, 3, "GPS is off (GPS page)");
  else if (!N.link.hasFix(now)) row(d, 3, "GPS: no fix yet");
  else { snprintf(b, sizeof(b), "GPS fix, %d sats", (int)N.link.position().sats); row(d, 3, b); }
  row(d, 4, armed ? NOVA_HOLD " again: CLEAR" : NOVA_HOLD " x2: clear trip");
}

// A line made of small squares: the display has no line primitive.
static inline void line(DisplayDriver& d, int x0, int y0, int x1, int y1) {
  int dx = x1 - x0, dy = y1 - y0;
  int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy));
  if (n == 0) { d.fillRect(x0, y0, 2, 2); return; }
  for (int i = 0; i <= n; i += 1) d.fillRect(x0 + dx * i / n, y0 + dy * i / n, 2, 2);
}

// ---- BACK: which way is the last place that had a usable uplink --------------------------------
static inline void pageBack(DisplayDriver& d, nova::Nova& N, uint32_t now, bool gps_on) {
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  char b[48];
  row(d, 0, "BACK TO COVERAGE");
  if (!gps_on) { row(d, 2, "needs GPS: turn it on"); row(d, 3, "on the GPS page"); return; }
  if (!N.link.hasFix(now)) { row(d, 2, "waiting for a GPS fix"); return; }
  if (N.linkUsable()) {
    row(d, 2, "IN COVERAGE");
    char id[8]; uplinkId(N.link.entry(N.link.best()), id, sizeof(id));
    snprintf(b, sizeof(b), "best uplink %s", id); row(d, 3, b);
    return;
  }
  const lodestar::Position& p = N.link.position();
  double dist, brg;
  if (!N.trip.toCoverage(p.lat_e6, p.lon_e6, dist, brg)) { row(d, 2, "no coverage seen yet"); row(d, 3, "drive on; I will note it"); return; }
  // Arrow: relative to the way you are heading when moving, otherwise north is up.
  bool rel = N.link.moving(now) && p.course_valid;
  double ang = rel ? brg - (double)p.course_deg : brg;
  double a = ang * 3.14159265358979 / 180.0;
  int cx = 28, cy = 42, R = 15;
  int tipx = cx + (int)lround(R * sin(a)), tipy = cy - (int)lround(R * cos(a));
  int tailx = cx - (int)lround(R * 0.6 * sin(a)), taily = cy + (int)lround(R * 0.6 * cos(a));
  line(d, tailx, taily, tipx, tipy);
  for (int s = -1; s <= 1; s += 2) {
    double h = a + 3.14159265358979 + s * 0.5;
    line(d, tipx, tipy, tipx + (int)lround(7 * sin(h)), tipy - (int)lround(7 * cos(h)));
  }
  if (dist < 1000.0) snprintf(b, sizeof(b), "%d m", (int)(dist + 0.5));
  else if (dist < 10000.0) snprintf(b, sizeof(b), "%.1fkm", dist / 1000.0);
  else snprintf(b, sizeof(b), "%dkm", (int)(dist / 1000.0 + 0.5));
  d.setTextSize(2); d.setCursor(60, 28); d.print(b); d.setTextSize(1);   // at most 5 characters, so it fits
  unsigned long mins = N.trip.coverageAgeMs(now) / 60000u;
  snprintf(b, sizeof(b), "seen %lum ago", mins);
  d.setCursor(56, 47); d.print(b);
  d.setCursor(56, 56); d.print(rel ? "ahead = up" : "north = up");
}

// ---- BATT -------------------------------------------------------------------------------------
static inline void pageBatt(DisplayDriver& d, nova::Nova& N, uint32_t now) {
  (void)now;
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  char b[48];
  const nova::Battery& B = N.batt;
  if (B.pct() < 0) { row(d, 0, "BATTERY"); row(d, 1, "no reading yet"); return; }
  snprintf(b, sizeof(b), "BATT %d%% %d.%02dV%s", B.pct(), B.mv() / 1000, (B.mv() % 1000) / 10, B.charging() ? " chg" : "");
  row(d, 0, b);
  float tr, hl;
  if (B.charging()) row(d, 1, "charging");
  else if (B.trendPerHour(tr)) { snprintf(b, sizeof(b), "trend %+.1f %%/h", (double)tr); row(d, 1, b); }
  else row(d, 1, "trend: needs 30 min");
  if (!B.charging() && B.hoursLeft(hl)) {
    int h = (int)hl, m = (int)((hl - (float)h) * 60.0f);
    if (h >= 99) snprintf(b, sizeof(b), "~99+ h left"); else snprintf(b, sizeof(b), "~%dh%02dm left (est)", h, m);
    row(d, 2, b);
  } else row(d, 2, "");
  // history graph: up to 96 bars of 1 px, oldest left, 100% = 22 px
  int n = B.sampleCount(), gx = 0, gy = 62, gh = 22;
  int start = n > 120 ? n - 120 : 0;
  for (int i = start; i < n; i++) {
    int h = B.percent(B.sampleMv(i)) * gh / 100; if (h < 1) h = 1;
    d.fillRect(gx + (i - start), gy - h, 1, h);
  }
}

// ---- NOVA: settings and diagnostics --------------------------------------------------------------
static inline void pageSettings(DisplayDriver& d, nova::Nova& N, uint32_t now, uint32_t free_heap) {
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  char b[48];
  snprintf(b, sizeof(b), NOVA_PRODUCT " %s", NOVA_VERSION); row(d, 0, b);
  snprintf(b, sizeof(b), "profile %s (" NOVA_HOLD ")", nova::profileParams(N.cfg.profile).name); row(d, 1, b);
  if (N.cfg.quiet_on) snprintf(b, sizeof(b), "quiet %02d-%02d (next page)", N.cfg.quiet_start, N.cfg.quiet_end);
  else snprintf(b, sizeof(b), "quiet off (next page)");
  row(d, 2, b);
  if (N.diag.have_rx) snprintf(b, sizeof(b), "rx %lu tx %lu snr%+.0f", (unsigned long)N.diag.rx, (unsigned long)N.diag.tx, (double)N.diag.rx_snr);
  else snprintf(b, sizeof(b), "rx %lu tx %lu", (unsigned long)N.diag.rx, (unsigned long)N.diag.tx);
  row(d, 3, b);
  unsigned long up = now / 1000u;
  if (free_heap) snprintf(b, sizeof(b), "up %luh%02lum  %luk free", up / 3600u, (up / 60u) % 60u, (unsigned long)(free_heap / 1024u));
  else snprintf(b, sizeof(b), "up %luh%02lum", up / 3600u, (up / 60u) % 60u);
  row(d, 4, b);
}

// ---- QUIET: night-time silence -------------------------------------------------------------------
static inline void pageQuiet(DisplayDriver& d, nova::Nova& N, int hour) {
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  char b[48];
  row(d, 0, "QUIET HOURS");
  if (N.cfg.quiet_on) {
    snprintf(b, sizeof(b), "ON  %02d:00 to %02d:00", N.cfg.quiet_start, N.cfg.quiet_end); row(d, 1, b);
    row(d, 2, hour < 0 ? "clock not set: sound on" : (N.quietNow(hour) ? "quiet right now" : "not quiet right now"));
  } else { row(d, 1, "OFF"); row(d, 2, "sound and screen as usual"); }
  row(d, 3, "favourites still alert");
  row(d, 4, NOVA_HOLD ": turn on / off");
}

// ---- boot splash ----------------------------------------------------------------------------------
static inline void splash(DisplayDriver& d, const char* meshcore_version) {
  int mid = d.width() / 2;
  d.setColor(UIColor::corp_blue); d.setTextSize(2);
  d.drawTextCentered(mid, 2, "LODESTAR");
  d.drawTextCentered(mid, 19, NOVA_PRODUCT);
  d.setColor(UIColor::primary_txt); d.setTextSize(1);
  d.drawTextCentered(mid, 40, "by WRS");
  char b[32]; snprintf(b, sizeof(b), "MeshCore %.12s", meshcore_version);
  d.setColor(UIColor::secondary_txt);
  d.drawTextCentered(mid, 53, b);
}

// ---- key handling shared by the pages -------------------------------------------------------------
// Trip reset needs two presses within 4 seconds, so a stray press never wipes a trip.
static inline uint32_t& tripArmedAt() { static uint32_t at = 0; return at; }
static inline bool& tripArmedFlag() { static bool f = false; return f; }
static inline bool tripIsArmed(uint32_t now) { return tripArmedFlag() && nova::elapsed(now, tripArmedAt()) < 4000u; }
static inline bool tripPress(uint32_t now) {      // true when this press should clear the trip
  if (tripIsArmed(now)) { tripArmedFlag() = false; return true; }
  tripArmedFlag() = true; tripArmedAt() = now;
  return false;
}

static inline void cycleProfile(nova::Nova& N) {
  N.cfg.profile = (uint8_t)((N.cfg.profile + 1) % 3);
  N.applyProfile(); N.settings_dirty = true;
}
static inline void toggleQuiet(nova::Nova& N) {
  N.cfg.quiet_on = !N.cfg.quiet_on; N.settings_dirty = true;
}

}  // namespace novaui
