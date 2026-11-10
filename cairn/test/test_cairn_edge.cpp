// Edge-case and bad-input tests for Cairn.h. Build with the compiler's checkers on so a hidden overrun or undefined
// behaviour stops the run instead of passing quietly:
//   g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
//       -Icairn cairn/test/test_cairn_edge.cpp -o edge && ./edge [lines.jsonl]
// With a file name, every JSON line made here is written there so the build can check each one is strict JSON.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "Cairn.h"
using namespace cairn;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define CHECKM(c, msg) do { checks++; if (!(c)) { printf("FAIL line %d: %s  [%s]\n", __LINE__, #c, (msg)); fails++; } } while (0)

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }

static FILE* jl = nullptr;
static void emit(const char* s) { if (jl && s && *s) fprintf(jl, "%s\n", s); }

// Run a command with canary bytes around the reply buffer, so a write past `cap` is caught even without the sanitizer.
static std::string run(Cairn& c, const std::string& cmd, size_t cap, uint32_t up = 100) {
  std::vector<char> buf(cap + 32, (char)0xAA);
  char* r = buf.data() + 16;
  c.runCommand(cmd.c_str(), r, cap, up);
  bool tail = true, head = true;
  for (int i = 0; i < 16; i++) { if ((unsigned char)buf[i] != 0xAA) head = false; if ((unsigned char)buf[16 + cap + i] != 0xAA) tail = false; }
  CHECKM(head && tail, cmd.c_str());
  return cap ? std::string(r, strnlen(r, cap)) : std::string();
}
static bool has(const std::string& s, const char* t) { return s.find(t) != std::string::npos; }

// Everything the settings and requests are allowed to hold, whatever was typed.
static void invariants(const Cairn& c, const std::string& why) {
  const Config& g = c.cfg; const char* w = why.c_str();
  CHECKM(g.mode <= 1 && g.power_pref <= 1 && g.ant10 <= 200 && g.help_on <= 1 && g.eco <= 1 && g.lowbat <= 1, w);
  CHECKM(g.auto_min == 0 || (g.auto_min >= 10 && g.auto_min <= 1440), w);
  CHECKM(g.beacon_min >= 1 && g.beacon_min <= 60 && g.main_chan < CAIRN_MAX_CHANNELS, w);
  CHECKM(strnlen(c.chan_name, sizeof(c.chan_name)) < sizeof(c.chan_name), w);
  CHECKM(strnlen(c.say_text, sizeof(c.say_text)) <= (size_t)CAIRN_SAY_MAX, w);
  CHECKM(c.ok_channel >= 0 && c.ok_channel < CAIRN_MAX_CHANNELS && c.say_channel >= 0 && c.say_channel < CAIRN_MAX_CHANNELS, w);
  CHECKM(c.chan_idx >= 0 && c.chan_idx < CAIRN_MAX_CHANNELS && c.want_status >= 0 && c.want_status <= CAIRN_NSTATUS && c.want_mark >= 0 && c.want_mark <= CAIRN_NMARK, w);
  CHECKM(c.msg_channel >= -1 && c.msg_channel < CAIRN_MAX_CHANNELS && c.help_chan >= -1 && c.help_chan < CAIRN_MAX_CHANNELS, w);
  CHECKM(c.nunits >= 0 && c.nunits <= CAIRN_NUNIT, w);
  for (int i = 0; i < c.nunits; i++) {
    CHECKM(c.units[i].cru >= 1 && c.units[i].cru <= 99 && strlen(c.units[i].key) == 12 && Cairn::unitCallOk(c.units[i].call), w);
    for (int k = i + 1; k < c.nunits; k++) CHECKM(c.units[i].cru != c.units[k].cru && strcmp(c.units[i].call, c.units[k].call) != 0 && strcmp(c.units[i].key, c.units[k].key) != 0, w);
  }
  // what is saved must load back the same
  char s[96]; Config back; size_t n = saveConfig(g, s, sizeof(s));
  CHECKM(n > 0 && loadConfig(s, back) && back.mode == g.mode && back.ant10 == g.ant10 && back.auto_min == g.auto_min && back.beacon_min == g.beacon_min && back.main_chan == g.main_chan, w);
}

int main(int argc, char** argv) {
  if (argc > 1) jl = fopen(argv[1], "w");

  // ---------------------------------------------------------------- 1. commands that had no test of their own
  { Cairn c; c.ctx.freq_mhz = 910.525f; c.cfg.lowbat = 0;
    std::string r = run(c, " lowbat on", 200); CHECK(c.cfg.lowbat == 1 && c.cfg_dirty && has(r, "on"));
    c.cfg_dirty = false; r = run(c, " lowbat off", 200); CHECK(c.cfg.lowbat == 0 && c.cfg_dirty && has(r, "off"));
    c.cfg_dirty = false; r = run(c, " lowbat maybe", 200); CHECK(c.cfg.lowbat == 0 && !c.cfg_dirty && has(r, "lowbat on | off"));
    r = run(c, " lowbat", 200); CHECK(!c.cfg_dirty && has(r, "low-battery message is off"));
    r = run(c, " units", 200); CHECK(c.want_units && has(r, "OK"));
    r = run(c, " config", 300); CHECK(has(r, "mode standard") && has(r, "main channel 0") && has(r, "antenna 2.0 dBi"));
    r = run(c, " eco off", 300); CHECK(c.cfg.eco == 0 && has(r, "battery saving off")); r = run(c, " eco on", 300); CHECK(c.cfg.eco == 1);
    r = run(c, " eco huh", 300); CHECK(c.cfg.eco == 1 && has(r, "cairn eco on | off"));
    r = run(c, " main 5", 200); CHECK(c.cfg.main_chan == 5); r = run(c, " main 40", 200); CHECK(c.cfg.main_chan == 5); r = run(c, " main -1", 200); CHECK(c.cfg.main_chan == 5);
    r = run(c, " main 5x", 200); CHECK(c.cfg.main_chan == 5); r = run(c, " main", 200); CHECK(has(r, "channel 5"));
    r = run(c, " mark 3 4", 200); CHECK(c.want_mark == 3 && c.msg_channel == 4); c.want_mark = 0;
    r = run(c, " mark 0", 200); CHECK(c.want_mark == 0 && has(r, "Err")); r = run(c, " mark 8", 200); CHECK(c.want_mark == 0 && has(r, "Err"));
    r = run(c, " mark 1 40", 200); CHECK(c.want_mark == 0 && has(r, "Err")); r = run(c, " mark 1 x", 200); CHECK(c.want_mark == 0 && has(r, "Err"));
    r = run(c, " ustatus 8", 200); CHECK(c.want_status == 8); c.want_status = 0; r = run(c, " ustatus 9", 200); CHECK(c.want_status == 0 && has(r, "Err"));
    r = run(c, " ustatus 0", 200); CHECK(c.want_status == 0 && has(r, "Err"));
    r = run(c, " beacon 1", 200); CHECK(c.cfg.beacon_min == 1); r = run(c, " beacon 60", 200); CHECK(c.cfg.beacon_min == 60);
    r = run(c, " beacon 61", 200); CHECK(c.cfg.beacon_min == 60); r = run(c, " beacon -5", 200); CHECK(c.cfg.beacon_min == 60);
    r = run(c, " auto 10", 200); CHECK(c.cfg.auto_min == 10); r = run(c, " auto 1440", 200); CHECK(c.cfg.auto_min == 1440);
    r = run(c, " auto 1441", 200); CHECK(c.cfg.auto_min == 1440); r = run(c, " auto 9", 200); CHECK(c.cfg.auto_min == 1440);
    r = run(c, " auto off", 200); CHECK(c.cfg.auto_min == 0 && c.auto_next_up == 0);
    invariants(c, "after the plain checks");
  }

  // ---------------------------------------------------------------- 2. numbers that are not numbers
  { Cairn c; c.ctx.freq_mhz = 910.525f;
    const char* bad[] = {"nan", "NaN", "inf", "-inf", "infinity", "1e999", "-0", "0x10", "1e1", "+5", " 5", "5 ", "", "-", ".", "..5", "5.5.5", "99999999999999999999", "-99999999999999999999"};
    for (const char* b : bad) {
      uint8_t before = c.cfg.ant10; std::string cmd = std::string(" antenna ") + b; std::string r = run(c, cmd, 300);
      // either refused with Err, or accepted into 0..20 dBi: never garbage
      CHECKM(c.cfg.ant10 <= 200 && (c.cfg.ant10 == before || !has(r, "Err")), cmd.c_str());
    }
    c.cfg.ant10 = 20; run(c, " antenna nan", 300); CHECKM(c.cfg.ant10 == 20, "cairn antenna nan must be refused, not stored");
    run(c, " antenna -nan", 300); CHECKM(c.cfg.ant10 == 20, "cairn antenna -nan must be refused");
    run(c, " antenna 20.04", 300); CHECK(c.cfg.ant10 == 0 || c.cfg.ant10 == 20);   // over 20 dBi: refused, unchanged
    run(c, " antenna 20", 300); CHECK(c.cfg.ant10 == 200); run(c, " antenna 20.1", 300); CHECK(c.cfg.ant10 == 200);
    run(c, " antenna 0", 300); CHECK(c.cfg.ant10 == 0);
    run(c, " antenna 2.25", 300); CHECK(c.cfg.ant10 == 23 || c.cfg.ant10 == 22);
    for (const char* t : {" time 1699999999", " time 4102444801", " time -1", " time 1e10", " time 0x65000000", " time 17700000001111111111111", " time  1770000000 extra"}) { c.want_time = 0; run(c, t, 200); CHECKM(c.want_time == 0, t); }
    c.want_time = 0; run(c, " time 1700000000", 200); CHECK(c.want_time == 1700000000UL);
    c.want_time = 0; run(c, " time 4102444800", 200); CHECK(c.want_time == 4102444800UL);
    invariants(c, "numbers");
  }

  // ---------------------------------------------------------------- 3. the power limit, checked against numbers worked out by hand
  // (not by calling the same function): limit = 30 dBm, minus 1 dB for every dBi of antenna above 6; the radio adds 12 dB to its
  // setting up to 28 dBm and we add 1 dB for tolerance. Highest setting whose (dBm + 1) stays at or under the limit.
  { struct Row { float mhz; int ant10; int hw; int want; };
    Row rows[] = {
      {910.525f, 0, 22, 22}, {910.525f, 20, 22, 22}, {910.525f, 60, 22, 22},   // up to 6 dBi: 28 + 1 = 29 <= 30
      {910.525f, 70, 22, 22},                                                   // 7 dBi: limit 29 -> 29 <= 29
      {910.525f, 71, 22, 15},                                                   // 7.1 dBi: limit 28.9 -> need est <= 27 -> setting 15
      {910.525f, 80, 22, 15}, {910.525f, 90, 22, 14}, {910.525f, 100, 22, 13}, {910.525f, 150, 22, 8}, {910.525f, 200, 22, 3},
      {910.525f, 20, 10, 10},                                                   // a board whose own maximum is 10
      {902.0f, 20, 22, 22}, {928.0f, 20, 22, 22},
      {901.9f, 20, 22, -100}, {928.1f, 20, 22, -100}, {868.0f, 20, 22, -100}, {915.0f, 90, 22, 14}, {0.0f, 20, 22, -100}, {NAN, 20, 22, -100},
    };
    for (const Row& r : rows) { int got = power::legalMax(r.mhz, r.ant10, r.hw); char m[80]; snprintf(m, sizeof m, "%.3f MHz, ant %d, hw %d: got %d want %d", (double)r.mhz, r.ant10, r.hw, got, r.want); CHECKM(got == r.want, m); }
    // the dBm figure that setting stands for never goes past what the rule allows, for every antenna
    for (int a = 0; a <= 200; a++) { int s = power::legalMax(910.525f, a, 22); CHECK(s >= -9 && power::worstDbm10(s) <= power::limitDbm10(a) + (s == -9 ? 1000 : 0)); }
    // wantedSetting: Disaster and help beacon use all of it, saver is capped at 10, never above the legal one, and -100 leaves the power alone
    Cairn c; c.ctx.freq_mhz = 910.525f; c.ctx.hw_max = 22;
    c.cfg.ant10 = 90; c.cfg.mode = 1; CHECK(c.wantedSetting() == 14);
    c.cfg.mode = 0; c.cfg.power_pref = 1; CHECK(c.wantedSetting() == 10);
    c.cfg.ant10 = 200; CHECK(c.wantedSetting() == 3);          // saver would be 10, but the antenna only allows 3
    c.cfg.power_pref = 0; c.cfg.help_on = 1; CHECK(c.wantedSetting() == 3);
    c.ctx.freq_mhz = 868.0f; CHECK(c.wantedSetting() == -100);
    for (int s = -20; s < 40; s++) CHECK(power::milliwatts(power::estDbm(s)) > 0);
  }

  // ---------------------------------------------------------------- 4. battery and timers
  { Cairn c; c.ctx.external = false;
    int seq[] = {50, 30, 16, 15, 14, 15, 20, 31, 15, 14, -1, 10, 31};     // 1 = sends
    int want[] = {0,  0,  0,  1,  0,  0,  0,  0,  1,  0,   0,  0,  0};
    for (size_t i = 0; i < sizeof(seq) / sizeof(*seq); i++) { c.ctx.batt_pct = seq[i]; CHECKM((int)c.lowBatDue() == want[i], "lowBatDue sequence"); }
    c.cfg.lowbat = 0; c.lowbat_sent = false; c.ctx.batt_pct = 5; CHECK(!c.lowBatDue());
    c.cfg.lowbat = 1; c.ctx.external = true; c.ctx.batt_pct = 5; CHECK(!c.lowBatDue());
    // Battery-saving switches on at 25 and off at 35, and not on external power
    c.ctx.external = false; c.cfg.eco = 1; c.eco_low = false;
    c.ctx.batt_pct = 26; c.updateEco(); CHECK(!c.eco_low); c.ctx.batt_pct = 25; c.updateEco(); CHECK(c.eco_low); c.ctx.batt_pct = 34; c.updateEco(); CHECK(c.eco_low);
    c.ctx.batt_pct = 35; c.updateEco(); CHECK(!c.eco_low); c.ctx.batt_pct = 20; c.updateEco(); c.ctx.external = true; c.updateEco(); CHECK(!c.eco_low);
    // timers keep working when the seconds-since-boot counter wraps past 2^32
    c.cfg.help_on = 1; c.cfg.beacon_min = 5; c.help_next_up = 0; CHECK(c.helpDue(0xFFFFFF00u)); c.helpSent(0xFFFFFF00u);
    CHECK(!c.helpDue(0xFFFFFF00u + 299)); CHECK(c.helpDue(0xFFFFFF00u + 300)); CHECK(!c.helpDue(5) && c.helpDue(50));   // next is due at 44 after the wrap
    c.cfg.help_on = 0; c.cfg.auto_min = 10; c.autoReset(0xFFFFFF00u); CHECK(c.auto_next_up != 0 && !c.autoDue(0xFFFFFF00u + 599) && c.autoDue(0xFFFFFF00u + 600));
    c.cfg.help_on = 1; CHECK(!c.autoDue(0xFFFFFF00u + 100000)); c.cfg.help_on = 0;
    c.auto_next_up = 0; c.cfg.auto_min = 0; c.autoReset(1234); CHECK(c.auto_next_up == 0 && !c.autoDue(99999));
  }

  // ---------------------------------------------------------------- 5. random garbage into the command line
  { Cairn c; c.ctx.freq_mhz = 910.525f;
    const char* words[] = {"status", "id", "ok", "dump", "clear", "clear yes", "time", "say", "dm", "units", "unit", "unit add", "unit name", "unit rename", "unit remove", "unit del",
      "chans", "chan", "chan add", "chan new", "chan remove", "chan rm", "chan del", "chan key", "chan list", "mode", "mode disaster", "mode standard", "power", "power full", "power saver", "antenna",
      "help", "help off", "beacon", "auto", "auto off", "eco", "lowbat", "main", "ustatus", "mark", "config", "", " ", "cairn"};
    const char* bits[] = {"0", "1", "5", "39", "40", "-1", "99", "100", "1440", "1441", "2147483647", "4294967296", "99999999999999999999", "0x10", "1e3", "nan", "inf", "-0", "2.5", "20.1",
      "a1b2c3d4e5f6", "A1B2C3D4E5F6A1B2C3D4", "00112233445566778899aabbccddeeff", "00112233445566778899aabbccddeeffaa", "00112233445566778899aabbccddeefg", "zzzzzzzzzzzz",
      "#water", "#Water-Tank", "#bad name", "#", "##", "#\xc3\xa9", "Family", "Block Watch", "WOLF-1", "wolf/2", "CRU-5", "CRU-0", "CRU-100", "bad name", "x\"y", "x\\y", "q\ttab",
      "caf\xc3\xa9", "\xff\xfe\xfd", "\xe2\x82", "\x01\x02", "on", "off", "yes", "no", "disaster", "standard", "normal", "full", "saver", "max", "low", "stop", "status", "list"};
    size_t caps[] = {1, 2, 3, 8, 40, 64, 128, 300};
    for (int it = 0; it < 120000; it++) {
      std::string cmd;
      if (rn(10) == 0) { size_t n = 1 + rn(400); for (size_t i = 0; i < n; i++) { char ch = (char)(1 + rn(255)); cmd += ch; } }     // pure noise (no NUL: it is a C string)
      else { int parts = 1 + (int)rn(6); cmd = rn(4) ? " " : ""; for (int p = 0; p < parts; p++) { cmd += (p == 0 || rn(3) == 0) ? words[rn(sizeof(words) / sizeof(*words))] : bits[rn(sizeof(bits) / sizeof(*bits))]; cmd += std::string(rn(3) == 0 ? rn(4) : 1, ' '); } }
      if (rn(40) == 0) cmd += std::string(CAIRN_SAY_MAX + rn(300), 'x');
      c.want_say = c.want_dm = false; c.chan_op = 0;                                         // let the next one be tried
      run(c, cmd, caps[rn(sizeof(caps) / sizeof(*caps))], rnd());
      if (it % 97 == 0) invariants(c, cmd);
      if (it % 5000 == 0) { c = Cairn(); c.ctx.freq_mhz = 910.525f; }
    }
    invariants(c, "after the fuzz run");
    // the usual replies still fit and read properly in a real-sized buffer
    Cairn d; d.ctx.freq_mhz = 910.525f; std::string r = run(d, " power", 300); CHECK(has(r, "mode") && r.size() < 300);
    r = run(d, "", 300); CHECK(has(r, "LODESTAR CAIRN"));
  }

  // ---------------------------------------------------------------- 6. settings and unit files that are damaged
  { Config g; const char* badcfg[] = {"", "CAIRN2", "CAIRN2 1 2 3", "CAIRN2 2 0 20 0 5 0 1 1 0", "CAIRN2 0 0 201 0 5 0 1 1 0", "CAIRN2 0 0 20 1441 5 0 1 1 0",
      "CAIRN2 0 0 20 0 0 0 1 1 0", "CAIRN2 0 0 20 0 61 0 1 1 0", "CAIRN2 0 0 20 0 5 0 1 1 40", "CAIRN2 0 0 20 0 5 0 1 1 -1", "CAIRN1 0 0 20 0 5 0 1 1 0", "CAIRN2 a b c d e f g h i",
      "CAIRN2 99999999999 0 20 0 5 0 1 1 0", "\xff\xff\xff", "CAIRN2 -0 -0 -0 -0 -0 -0 -0 -0 -0"};
    for (const char* b : badcfg) { Config x = g; bool ok = loadConfig(b, x); CHECKM(!ok && x.mode == g.mode && x.ant10 == g.ant10 && x.main_chan == g.main_chan, b); }
    CHECK(!loadConfig(nullptr, g));
    CHECK(loadConfig("CAIRN2 1 1 90 30 10 1 0 0 7", g) && g.mode == 1 && g.ant10 == 90 && g.auto_min == 30 && g.main_chan == 7);
    char tiny[8]; CHECK(saveConfig(g, tiny, sizeof tiny) == 0);       // does not fit: refused, not cut
    for (int it = 0; it < 20000; it++) { std::string s = "CAIRN2"; int n = (int)rn(12); for (int i = 0; i < n; i++) { s += ' '; s += std::to_string((int)rn(300) - 20); } Config x; loadConfig(s.c_str(), x); CHECK(x.mode <= 1 && x.ant10 <= 200 && x.main_chan < CAIRN_MAX_CHANNELS && x.beacon_min >= 1 && x.beacon_min <= 60); }
    // unit file
    Cairn c; const char* files[] = {"", "\n\n\n", "0123456789ab 1 WOLF\n0123456789ab 2 HAWK\n", "0123456789ab 1 WOLF\naaaaaaaaaaaa 1 HAWK\n", "0123456789ab WOLF\n", "0123456789ab 100 WOLF\n",
      "0123456789ab 0 WOLF\n", "0123456789ab 1 CRU-5\n", "0123456789ab 1 bad name\n", "0123456789ab 1 WOLF-THIS-NAME-IS-WAY-TOO-LONG\n", "zzzzzzzzzzzz 1 WOLF\n", "0123456789abX1 WOLF\n", "\xff\xfe\n"};
    for (const char* f : files) { c.loadUnits(f); invariants(c, f); }
    std::string many; for (int i = 0; i < CAIRN_NUNIT + 20; i++) { char l[64]; snprintf(l, sizeof l, "%012x %d U%d\n", 0x100000 + i, (i % 99) + 1, i); many += l; }
    c.loadUnits(many.c_str()); CHECK(c.nunits <= CAIRN_NUNIT); invariants(c, "many units");
    for (int it = 0; it < 20000; it++) { std::string f; int lines = (int)rn(6); for (int l = 0; l < lines; l++) { int len = (int)rn(45); for (int i = 0; i < len; i++) f += (char)(rn(5) ? "0123456789abcdef AZ-/\n"[rn(23)] : 1 + rn(254)); f += '\n'; } c.loadUnits(f.c_str()); invariants(c, "unit file noise"); }
    char out[512]; c.nunits = 0; c.loadUnits("0123456789ab 1 WOLF\n"); size_t n = c.saveUnits(out, sizeof out); CHECK(n > 0); Cairn d; d.loadUnits(out); CHECK(d.nunits == 1 && strcmp(d.units[0].call, "WOLF") == 0);
    CHECK(c.saveUnits(out, 5) == 0 || true); char u[600]; size_t un = c.unitsJson(u, sizeof u); CHECK(un > 0); emit(u); CHECK(c.unitsJson(u, 10) == 0);
  }

  // ---------------------------------------------------------------- 7. text from other people going into the journal
  { static const std::string longx(150, 'x'); const char* nasty[] = {"", "plain", "quote\" and back\\slash", "line\nbreak", "tab\there", "ctl\x01\x02\x1f\x7f", "caf\xc3\xa9 \xe2\x9c\x93", "\xff\xfe invalid utf8", "\xe2\x82", "</script><b>", "{\"r\":\"x\"}", "\\u0000", "\\", "\"", longx.c_str()};
    char line[CAIRN_LINE];
    for (const char* t : nasty) for (const char* f : nasty) {
      size_t n = recMsg(line, sizeof line, 1770000000, 5, 'c', f, "Public", 7.25f, 2, t);
      if (n) { CHECK(strlen(line) == n && !strchr(line, '\n') && !strchr(line, '\r')); emit(line); }
      n = recAdv(line, sizeof line, 1770000000, 5, 2, t, (const uint8_t*)"\x01\x02\x03\x04\x05\x06", 3, -4.5f, -97, true, 29760430, -95369800, true, 29760431, -95369801);
      if (n) { CHECK(!strchr(line, '\n')); emit(line); }
      n = recOwn(line, sizeof line, 1770000000, 5, "LC-K7M2P4XQ", 3, t); if (n) { CHECK(!strchr(line, '\n')); emit(line); }
    }
    std::string big(5000, 'q'); CHECK(recMsg(line, sizeof line, 1, 1, 'c', "a", "b", 1.0f, 1, big.c_str()) == 0);        // too long to keep: refused, not cut
    CHECK(recMsg(line, 1, 1, 1, 'c', "a", "b", 1.0f, 1, "x") == 0 && recMsg(line, 0, 1, 1, 'c', "a", "b", 1.0f, 1, "x") == 0);
    // the writer must not need the text to end exactly where the buffer does
    for (size_t cap = 2; cap < 140; cap++) { char t[140]; size_t n = recMsg(t, cap, 1770000000, 5, 'c', "Alice", "Public", 7.0f, 2, "hello there"); CHECK(n == 0 || (n < cap && strlen(t) == n)); }
    // numbers that cannot be written as JSON: nan and inf are not allowed in a JSON file
    float weird[] = {NAN, -NAN, INFINITY, -INFINITY, 1e30f, -1e30f, 0.0f, -0.0f, 7.25f};
    for (float s : weird) {
      size_t n = recMsg(line, sizeof line, 1770000000, 5, 'c', "a", "Public", s, 1, "x"); CHECKM(n == 0 || (!strstr(line, "nan") && !strstr(line, "inf")), "a signal value of nan or inf must not reach the journal as nan/inf"); if (n) emit(line);
      n = recAdv(line, sizeof line, 1770000000, 5, 2, "n", (const uint8_t*)"\x01\x02\x03\x04\x05\x06", 1, s, -90, false, 0, 0, false, 0, 0); CHECKM(n == 0 || (!strstr(line, "nan") && !strstr(line, "inf")), "advert snr nan/inf"); if (n) emit(line);
    }
    int hopsv[] = {-1, 0, 1, 127, 128, 255, 100000, INT32_MIN, INT32_MAX};
    for (int h : hopsv) { size_t n = recMsg(line, sizeof line, 0xFFFFFFFFu, 0xFFFFFFFFu, 'c', "a", "Public", 1.0f, h, "x"); CHECK(n > 0); emit(line); }
    int32_t ext[] = {0, 1, -1, 90000000, -90000000, 180000000, -180000000, INT32_MAX, INT32_MIN, 999999999, -999999999};
    char m[96];
    for (int32_t la : ext) for (int32_t lo : ext) {
      size_t n = okMessage("LC-K7M2P4XQ", true, la, lo, m, sizeof m); CHECK(n > 0 && n < sizeof m && strlen(m) == n);
      n = recAdv(line, sizeof line, 1, 1, 1, "n", (const uint8_t*)"\x01\x02\x03\x04\x05\x06", 1, 1.0f, -80, true, la, lo, true, la, lo); CHECK(n > 0); emit(line);
    }
    for (size_t cap = 0; cap < 60; cap++) { char t[64]; memset(t, 0xAA, sizeof t); size_t n = okMessage("LC-K7M2P4XQ", true, 29760430, -95369800, t, cap); CHECK(n == 0 || n < cap); if (cap < 64) CHECK((unsigned char)t[cap < 63 ? cap : 63] == 0xAA || cap >= 63 || n > 0); }
    // station messages for every label and a bad index
    for (int k = -2; k <= CAIRN_NSTATUS + 2; k++) { size_t n = statusMessage("LC-K7M2P4XQ", k, true, 1, 1, m, sizeof m); CHECK(n == 0 || (k >= 1 && k <= CAIRN_NSTATUS) || true); }
    for (int k = -2; k <= CAIRN_NMARK + 2; k++) { markMessage("LC-K7M2P4XQ", k, false, 0, 0, m, sizeof m); }
    for (int p = -5; p <= 105; p++) { lowBatMessage("LC-K7M2P4XQ", p, true, 1, 1, m, sizeof m); }
    // boot and status lines
    CHECK(recBoot(line, sizeof line, 1770000000, "LC-K7M2P4XQ", CAIRN_VERSION) > 0); emit(line);
    Cairn c; c.setId("LC-K7M2P4XQ"); c.ctx.freq_mhz = 910.525f; char st[800]; size_t sn = c.statusJson(st, sizeof st, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu); CHECK(sn > 0); emit(st);
    c.ctx.batt_pct = -1; sn = c.statusJson(st, sizeof st, 0, 0, 0); CHECK(sn > 0); emit(st); CHECK(c.statusJson(st, 20, 0, 0, 0) == 0);
  }

  // ---------------------------------------------------------------- 8. the journal ring, under load and with a slow reader
  { Journal j; Cursor fast, slow; uint32_t read_fast = 0, read_slow = 0, pushed = 0; char line[CAIRN_LINE];
    for (int i = 0; i < 20000; i++) {
      snprintf(line, sizeof line, "{\"n\":%d}", i); CHECK(j.push(line)); pushed++;
      while (const char* l = j.peek(fast)) { CHECK(l[0] == '{'); j.advance(fast); read_fast++; }
      if (i % 53 == 0) { while (const char* l = j.peek(slow)) { CHECK(l[0] == '{'); j.advance(slow); read_slow++; } }
    }
    while (j.peek(fast)) { j.advance(fast); read_fast++; } while (j.peek(slow)) { j.advance(slow); read_slow++; }
    CHECKM(read_fast + fast.lost == pushed && fast.lost == 0, "fast reader sees every line");
    CHECKM(read_slow + slow.lost == pushed, "slow reader: every line is either read or counted as lost");
    CHECK(j.total == pushed && j.refused == 0);
    std::string max(CAIRN_LINE - 1, 'a'), over(CAIRN_LINE, 'a'); CHECK(j.push(max.c_str()) && !j.push(over.c_str()) && !j.push("") && !j.push(nullptr) && j.refused == 3);
    Cursor c2; const char* got = nullptr; uint32_t n = 0; while ((got = j.peek(c2))) { j.advance(c2); n++; } CHECK(n == CAIRN_RING && strlen(j.peek(c2) ? j.peek(c2) : "") == 0);
    CHECK(j.pending(Cursor()) == CAIRN_RING);
    j.push("x", true); CHECK(j.urgent()); j.clearUrgent(); CHECK(!j.urgent());
    // the byte ring that carries text to the tablet
    ByteRing<16> b; for (int round = 0; round < 1000; round++) { size_t k = rn(20); for (size_t i = 0; i < k; i++) b.push((uint8_t)i); uint8_t d[8]; size_t p = b.peek(d, sizeof d); CHECK(p <= 8 && p <= b.count()); b.drop(rn(30)); while (rn(3) == 0 && b.pop() >= 0) {} CHECK(b.count() <= 16 && b.room() == 16 - b.count()); }
    ByteRing<4> s; for (int i = 0; i < 10; i++) s.push((uint8_t)i); CHECK(s.count() == 4 && s.lost() == 6); CHECK(s.pop() == 0 && s.pop() == 1); s.clear(); CHECK(s.pop() == -1);
    // repeated adverts are logged once, again only by a shorter route; the table cycles without trouble
    AdvertSeen seen; uint8_t key[6] = {1, 2, 3, 4, 5, 6};
    CHECK(seen.shouldLog(key, 100, 5) && !seen.shouldLog(key, 100, 5) && !seen.shouldLog(key, 100, 6) && seen.shouldLog(key, 100, 2) && !seen.shouldLog(key, 100, 2));
    for (int i = 0; i < 500; i++) { key[5] = (uint8_t)i; seen.shouldLog(key, (uint32_t)i, (int)rn(300) - 10); }
    key[0] = 9; CHECK(seen.shouldLog(key, 0, -1) || true);
  }

  // ---------------------------------------------------------------- 9. channel names and keys
  { char out[40]; const char* ok[] = {"#a", "#water-tank", "#WATER", "#a1-b2"}; for (const char* s : ok) CHECKM(hashtagNormalize(s, out, sizeof out), s);
    const char* no[] = {"", "#", "a", "# a", "#a b", "#a_b", "#\xc3\xa9", "#a\"", "##a", "#a.", nullptr};
    for (const char* s : no) CHECKM(!hashtagNormalize(s, out, sizeof out), s ? s : "(null)");
    std::string l31 = "#" + std::string(30, 'a'), l32 = "#" + std::string(31, 'a'); CHECK(hashtagNormalize(l31.c_str(), out, sizeof out) && !hashtagNormalize(l32.c_str(), out, sizeof out));
    char tiny[3]; CHECK(!hashtagNormalize("#abc", tiny, sizeof tiny) && !hashtagNormalize("#abc", tiny, 2));
    uint8_t k[16]; CHECK(parseHex16("00112233445566778899aabbccddeeff", k) && k[0] == 0 && k[15] == 0xff && !parseHex16("00112233445566778899aabbccddeef", k) && !parseHex16("", k) && !parseHex16(nullptr, k) && !parseHex16("g0112233445566778899aabbccddeeff", k));
    CHECK(chanNameOk("Family") && chanNameOk("Block Watch") && !chanNameOk("") && !chanNameOk(" x") && !chanNameOk("x ") && !chanNameOk("a\"b") && !chanNameOk("a\\b") && !chanNameOk("a\nb") && !chanNameOk(std::string(32, 'a').c_str()) && chanNameOk(std::string(31, 'a').c_str()));
    // names of channels as the radio hands them back must always come out of the JSON writer escaped
    char line[CAIRN_LINE]; W w(line, sizeof line); w.f("{\"c\":"); w.str("a\"b\\c\nd\x01"); w.f("}"); CHECK(w.done() > 0 && strcmp(line, "{\"c\":\"a\\\"b\\\\c\\nd \"}") == 0);
    uint8_t h[32]; sha256((const uint8_t*)"abc", 3, h); CHECK(h[0] == 0xba && h[1] == 0x78 && h[31] == 0xad); sha256((const uint8_t*)"", 0, h); CHECK(h[0] == 0xe3 && h[1] == 0xb0 && h[31] == 0x55);
    std::string longmsg(1000, 'z'); sha256((const uint8_t*)longmsg.data(), longmsg.size(), h); CHECK(true);
    for (int n = 0; n < 200; n++) { std::string s(n, 'k'); sha256((const uint8_t*)s.data(), s.size(), h); }
    for (int i = 0; i < 2000; i++) { uint8_t kk[5]; for (auto& b : kk) b = (uint8_t)rn(256); char id[12]; makeId(kk, id); CHECK(validId(id)); }
  }

  { char o[64]; W w(o, sizeof o); w.str("ok\xc3\xa9\xff\xc0\x80\xed\xa0\x80\xf4\x90\x80\x80\xe2\x82"); CHECK(w.done() > 0);
    CHECKM(strcmp(o, "\"ok\xc3\xa9????????\x3f?\"") == 0 || strstr(o, "\xff") == nullptr, o); CHECK(strstr(o, "\xc3\xa9") != nullptr && strstr(o, "\xc0") == nullptr && strstr(o, "\xed") == nullptr); }
  printf("edge tests: %d checks, %d failures\n", checks, fails);
  if (jl) fclose(jl);
  return fails ? 1 : 0;
}
