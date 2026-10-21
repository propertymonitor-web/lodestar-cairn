// Draws the CAIRN splash and station screen on a pretend 128x64 screen and checks nothing falls off the edges.
//   g++ -std=c++17 -Wall -Wextra -DNOVA_CAIRN=1 -Iinc -Inova/ui -Icairn/ui -isystem <MeshCore>/src cairn/test/test_cairn_ui.cpp
#include "CairnPages.h"
#include <string>
#include <vector>

ColorVal UIColor::window_bkg = 0, UIColor::title_bkg = 1, UIColor::title_txt = 1, UIColor::primary_txt = 1,
         UIColor::secondary_txt = 1, UIColor::warning_txt = 1, UIColor::popup_bkg = 0, UIColor::popup_txt = 1, UIColor::corp_blue = 1;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

struct Mock : DisplayDriver {
  int size = 1, cx = 0, cy = 0, bad = 0, xbm = 0;
  int bx = 0, by = 0, bw = 0, bh = 0;
  std::vector<std::string> lines;
  Mock() : DisplayDriver(128, 64) {}
  bool isOn() override { return true; }
  void turnOn() override {} void turnOff() override {} void clear() override {} void startFrame(ColorVal = 0) override {}
  void setTextSize(int s) override { size = s; }
  void setColor(ColorVal) override {}
  void setCursor(int x, int y) override { cx = x; cy = y; }
  uint16_t getTextWidth(const char* s) override { return (uint16_t)(strlen(s) * 6 * size); }
  void print(const char* s) override {
    lines.push_back(s);
    if (cx < 0 || cy < 0 || cx + (int)getTextWidth(s) > 128 || cy + 8 * size > 64) { bad++; printf("  off-screen text '%s' at %d,%d size %d\n", s, cx, cy, size); }
    if (xbm && cx < bx + bw + 2 && cy < by + bh && cx + (int)getTextWidth(s) > bx && cy + 8 * size > by) { bad++; printf("  text '%s' overlaps the emblem\n", s); }
  }
  void fillRect(int, int, int, int) override {}
  void drawRect(int, int, int, int) override {}
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {
    xbm++; bx = x; by = y; bw = w; bh = h;
    if (!bits || x < 0 || y < 0 || x + w > 128 || y + h > 64) { bad++; printf("  emblem off-screen\n"); }
  }
  void endFrame() override {}
  bool has(const char* needle) const { for (auto& l : lines) if (l.find(needle) != std::string::npos) return true; return false; }
};

int main() {
  { Mock m; cairnui::splash(m, "v1.15.0"); CHECK(m.bad == 0 && m.xbm == 1 && m.has("LODESTAR") && m.has("CAIRN") && m.has("FEAR REACTIVE") && m.has("Ver. " CAIRN_VERSION)); }
  { Mock m; cairnui::splash(m, "v10.200.3000-abcdef"); CHECK(m.bad == 0); }   // a long MeshCore version still fits
  CHECK(CAIRN_LOGO_W == 40 && CAIRN_LOGO_H == 60 && sizeof(cairn_logo) == (CAIRN_LOGO_W / 8) * CAIRN_LOGO_H);
  { cairn::Cairn C; Mock m; cairnui::pageStation(m, C, 100); CHECK(m.bad == 0 && m.has("CAIRN STATION") && m.has("ID not ready") && m.has("no check-in sent yet") && m.has("send check-in")); }
  { cairn::Cairn C; C.setId("LC-K7M2P4XQ"); C.file_bytes = 20480; C.last_ok_up = 61;
    Mock m; cairnui::pageStation(m, C, 100); CHECK(m.bad == 0 && m.has("LC-K7M2P4XQ") && m.has("log 20KB 0 lines") && m.has("check-in 40s ago"));
    Mock m2; cairnui::pageStation(m2, C, 700); CHECK(m2.bad == 0 && m2.has("check-in 10m ago"));
    Mock m3; cairnui::pageStation(m3, C, 20000); CHECK(m3.bad == 0 && m3.has("check-in 5.5h ago")); }
  if (fails) { printf("%d FAILED\n", fails); return 1; }
  printf("all CAIRN screen tests passed\n");
  return 0;
}
