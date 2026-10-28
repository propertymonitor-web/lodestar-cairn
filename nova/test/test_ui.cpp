// Draws every NOVA page on a pretend 128x64 screen and checks that nothing falls off the edges.
// Needs MeshCore's DisplayDriver.h:
//   g++ -std=c++17 -Wall -Wextra -Ilodestar -Inova -Inova/ui -I<MeshCore>/src nova/test/test_ui.cpp -o ui_test
#include "NovaPages.h"
#include <string>
#include <vector>

ColorVal UIColor::window_bkg = 0, UIColor::title_bkg = 1, UIColor::title_txt = 1, UIColor::primary_txt = 1,
         UIColor::secondary_txt = 1, UIColor::warning_txt = 1, UIColor::popup_bkg = 0, UIColor::popup_txt = 1, UIColor::corp_blue = 1;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

struct Mock : DisplayDriver {
  int size = 1, cx = 0, cy = 0, bad = 0, texts = 0, shapes = 0;
  std::vector<std::string> lines;
  Mock() : DisplayDriver(128, 64) {}
  bool isOn() override { return true; }
  void turnOn() override {} void turnOff() override {} void clear() override {} void startFrame(ColorVal = 0) override {}
  void setTextSize(int s) override { size = s; }
  void setColor(ColorVal) override {}
  void setCursor(int x, int y) override { cx = x; cy = y; }
  uint16_t getTextWidth(const char* s) override { return (uint16_t)(strlen(s) * 6 * size); }
  void print(const char* s) override {
    texts++; lines.push_back(s);
    if (cx < 0 || cy < 0 || cx + (int)getTextWidth(s) > 128 || cy + 8 * size > 64) { bad++; printf("  off-screen text '%s' at %d,%d size %d\n", s, cx, cy, size); }
  }
  void box(int x, int y, int w, int h) { shapes++; if (x < 0 || y < 0 || x + w > 128 || y + h > 64) bad++; }
  void fillRect(int x, int y, int w, int h) override { box(x, y, w, h); }
  void drawRect(int x, int y, int w, int h) override { box(x, y, w, h); }
  void drawXbm(int, int, const uint8_t*, int, int) override {}
  void endFrame() override {}
  bool has(const char* needle) const { for (auto& l : lines) if (l.find(needle) != std::string::npos) return true; return false; }
};

static uint32_t drive(nova::Nova& N, uint32_t t, int secs, int32_t& lat, int32_t lon, double mps) {
  for (int i = 0; i < secs; i++, t += 1000) { lat += (int32_t)(mps / 0.111319); N.link.onPosition(lat, lon, 9, true, t); N.loop(t, 3900, false); }
  return t;
}

int main() {
  using namespace novaui;
  // ---- an empty node: every page must still draw something sensible ----
  {
    nova::Nova N; uint32_t now = 5000; Mock m;
    pageLink(m, N, now);                    CHECK(m.has("NOTHING HEARD"));
    Mock m2; pageSent(m2, N, now);          CHECK(m2.has("no messages"));
    RangeData r{}; r.available = true; r.idle = true; Mock m3; pageRange(m3, N, r);   CHECK(m3.has("RANGE CHECK") && m3.has("start:"));
    Mock m4; pageTrip(m4, N, now, false, false);   CHECK(m4.has("TRIP 0 m") && m4.has("GPS is off"));
    Mock m5; pageBack(m5, N, now, false);   CHECK(m5.has("needs GPS"));
    Mock m6; pageBack(m6, N, now, true);    CHECK(m6.has("waiting for a GPS fix"));
    Mock m7; pageBatt(m7, N, now);          CHECK(m7.has("no reading"));
    Mock m8; pageSettings(m8, N, now, 0);   CHECK(m8.has(NOVA_PRODUCT " " NOVA_VERSION) && m8.has("profile car"));
    Mock m9; pageQuiet(m9, N, -1); CHECK(m9.has("OFF") && m9.bad == 0);
    N.cfg.quiet_on = true; N.cfg.quiet_start = 22; N.cfg.quiet_end = 7;
    Mock m10; pageQuiet(m10, N, 23); CHECK(m10.has("ON  22:00 to 07:00") && m10.has("quiet right now") && m10.bad == 0);
    Mock m11; pageQuiet(m11, N, 12); CHECK(m11.has("not quiet right now"));
    Mock m12; pageQuiet(m12, N, -1); CHECK(m12.has("clock not set"));
    N.cfg.quiet_on = false;
    CHECK(!(m.bad + m2.bad + m3.bad + m4.bad + m5.bad + m6.bad + m7.bad + m8.bad));
  }
  // ---- a busy node ----
  {
    nova::Nova N; uint32_t t = 100000; int32_t lat = 30000000, lon = -97000000;
    uint8_t a[8] = {0xA1,0xB2,0xC3}, b[8] = {0x44,0x55,0x66}, c[8] = {0x77,0x88,0x99}, d4[8] = {0x12,0x34,0x56};
    for (int i = 0; i < 12; i++, t += 20000) { N.link.onAdvert(a, 9.0f, -80, t); N.link.onAdvert(b, -6.0f, -105, t); N.link.onAdvert(c, 2.0f, -95, t); N.link.onAdvert(d4, -12.0f, -118, t); }
    t = drive(N, t, 120, lat, lon, 25.0);
    N.deliveries.onSend(1, true, 9000, "Bob", true, t - 20000); N.deliveries.onAck(1, t - 15000);
    N.deliveries.onSend(2, true, 9000, "Anastasia Long", true, t - 10000); uint8_t h[8] = {1,2,3,4,5,6,7,8}; N.deliveries.onTxText(h, t - 9000); N.deliveries.onHeard(h, 0x3F);
    N.deliveries.onSend(3, true, 3000, "Cy", false, t - 40000); N.deliveries.loop(t);
    for (int i = 0; i < 400; i++) N.batt.update((uint16_t)(4100 - i), false, t + (uint32_t)i * 150000u);
    Mock m; pageLink(m, N, t);              CHECK(m.has("UPLINK A1B2C3") && m.has("*A1B2C3") && m.has("heard 4"));
    Mock m2; pageSent(m2, N, t);            CHECK(m2.has("Bob") && m2.has("FAILED") && m2.has("heard by 3F"));
    RangeData r{}; r.available = true; r.count = 3; r.best_in = 8.0f; r.best_out = 6.5f; r.worst_in = -2.0f; r.sf = 10;
    Mock m3; pageRange(m3, N, r);           CHECK(m3.has("GOOD: 3 repeaters") && m3.has("best in +8.0"));
    r.count = 0; Mock m3b; pageRange(m3b, N, r);   CHECK(m3b.has("NONE: 0") && m3b.has("nobody answered"));
    r.running = true; r.count = 2; Mock m3c; pageRange(m3c, N, r);   CHECK(m3c.has("listening") && m3c.has("2 answered"));
    r.available = false; Mock m3d; pageRange(m3d, N, r);   CHECK(m3d.has("not in this build"));
    Mock m4; pageTrip(m4, N, t, true, true);       CHECK(m4.has("TRIP 2.9 km") && m4.has("GPS fix, 9 sats") && m4.has("hold again"));
    Mock m5; pageBack(m5, N, t, true);             CHECK(m5.has("IN COVERAGE"));
    for (int i = 0; i < 40; i++) { t += 60000; N.loop(t, 3900, false); }                       // coverage expires
    t = drive(N, t, 90, lat, lon, 25.0);                                                       // keep going north, no coverage
    Mock m6; pageBack(m6, N, t, true);             CHECK(m6.has("seen ") && m6.has("ahead = up") && m6.shapes > 10);
    Mock m6b; N.link.onPosition(lat, lon, 9, true, t + 100000); pageBack(m6b, N, t + 100000, true);   // stopped: north is up
    CHECK(m6b.has("north = up"));
    Mock m7; pageBatt(m7, N, t);                   CHECK(m7.has("BATT ") && m7.shapes > 20);
    Mock m8; pageSettings(m8, N, t, 180000);       CHECK(m8.has("k free"));
    CHECK(!(m.bad + m2.bad + m3.bad + m3b.bad + m3c.bad + m3d.bad + m4.bad + m5.bad + m6.bad + m6b.bad + m7.bad + m8.bad));
  }
  // ---- splash fits, and says who made it ----
  { Mock m; splash(m, "v1.15.0"); CHECK(m.bad == 0 && m.has("LODESTAR") && m.has(NOVA_PRODUCT) && m.has("by WRS") && m.has("MeshCore v1.15.0"));
    Mock m2; splash(m2, "v10.200.3000-abcdef"); CHECK(m2.bad == 0); }
  // ---- the arrow points the right way (checked on the squares it draws) ----
  {
    struct Probe : Mock { int minx = 999, maxx = -1, miny = 999, maxy = -1; void fillRect(int x, int y, int w, int h) override { Mock::fillRect(x, y, w, h); if (x < 50) { if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; } } };
    Probe p; line(p, 28, 57, 28, 27);            CHECK(p.minx == 28 && p.maxx == 28 && p.miny == 27 && p.maxy == 57);   // straight up
    Probe q; line(q, 10, 10, 40, 10);            CHECK(q.miny == 10 && q.maxy == 10 && q.minx == 10 && q.maxx == 40);
    Probe z; line(z, 5, 5, 5, 5);                CHECK(z.shapes == 1);
  }
  // ---- long names and big numbers never run off the screen ----
  {
    nova::Nova N; uint32_t t = 3600u * 1000u * 99u;
    N.deliveries.stats.sent = 99999; N.deliveries.stats.delivered = 99999; N.deliveries.stats.failed = 99999;
    N.diag.rx = 4000000000u; N.diag.tx = 4000000000u; N.diag.rx_snr = -19.0f; N.diag.have_rx = true;
    Mock m; pageSent(m, N, t); Mock m2; pageSettings(m2, N, t, 900000);
    CHECK(m.bad == 0 && m2.bad == 0);
  }
  // ---- trip reset needs two presses ----
  {
    CHECK(!tripPress(1000) && tripIsArmed(1500) && tripPress(2000) && !tripIsArmed(2100));
    CHECK(!tripPress(10000) && !tripIsArmed(15000) && !tripPress(15000));    // too slow: arms again instead of clearing
    nova::Nova N; cycleProfile(N); CHECK(N.cfg.profile == nova::PROFILE_DESK && N.settings_dirty);
    cycleProfile(N); CHECK(N.cfg.profile == nova::PROFILE_HIKE); cycleProfile(N); CHECK(N.cfg.profile == nova::PROFILE_CAR);
    N.settings_dirty = false; toggleQuiet(N); CHECK(N.cfg.quiet_on && N.settings_dirty);
  }
  printf(fails ? "\n%d FAILED\n" : "\nall ui tests passed\n", fails);
  return fails ? 1 : 0;
}
