// Host-side tests for Cairn.h:  g++ -std=c++17 -Wall -Wextra -Icairn cairn/test/test_cairn.cpp -o t && ./t [lines.jsonl]
// With a file name, the test also writes every journal line it makes there, so the build can check they are real JSON.
#include <string>
#include <stdio.h>
#include <string.h>
#include "Cairn.h"
using namespace cairn;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int legalExpect9() { return power::legalMax(910.525f, 90, 22); }

int main(int argc, char** argv) {
  FILE* jl = argc > 1 ? fopen(argv[1], "w") : nullptr;
  auto emit = [&](const char* s) { if (jl) fprintf(jl, "%s\n", s); };

  { // IDs: right shape, only the 32 clear characters, and different key bytes give different IDs
    uint8_t a[5] = {0, 1, 2, 3, 4}, b[5] = {255, 254, 253, 252, 251};
    char x[12], y[12];
    makeId(a, x); makeId(b, y);
    CHECK(strcmp(x, "LC-AAASEA2E") == 0);   // 00 01 02 03 04 as 8 groups of 5 bits
    CHECK(validId(x) && validId(y) && strcmp(x, y) != 0);
    for (int v = 0; v < 256; v++) { uint8_t r[5] = {(uint8_t)v, (uint8_t)(v * 7), (uint8_t)(v * 13), (uint8_t)(v * 31), (uint8_t)(v * 101)}; char z[12]; makeId(r, z); CHECK(validId(z)); }
    for (const char* c = ID_CHARS; *c; c++) CHECK(*c != 'I' && *c != 'O' && *c != '0' && *c != '1');
    CHECK(strlen(ID_CHARS) == 32);
    CHECK(!validId("LC-ABCDEFG")); CHECK(!validId("LC-ABCDEFGHJ")); CHECK(!validId("LC-ABCDE")); CHECK(!validId("XX-ABCDEFGH")); CHECK(!validId("LC-AB0DEFGH")); CHECK(!validId("LC-abcdefgh"));
    CHECK(!validId(nullptr)); CHECK(!validId(""));
  }
  { // the check-in text
    char m[96];
    size_t n = okMessage("LC-K7M2P4XQ", true, 29760430, -95369800, m, sizeof(m));
    CHECK(n > 0 && strcmp(m, "LC-K7M2P4XQ Reporting at: 29.76043,-95.36980") == 0);
    n = okMessage("LC-K7M2P4XQ", true, -33868820, 151209290, m, sizeof(m));
    CHECK(strcmp(m, "LC-K7M2P4XQ Reporting at: -33.86882,151.20929") == 0);
    n = okMessage("LC-K7M2P4XQ", true, -5, 5, m, sizeof(m));          // tiny values keep their leading zeros
    CHECK(strstr(m, "-0.00000,0.00001") || strstr(m, "-0.00001") || strstr(m, "0.00000"));
    n = okMessage("LC-K7M2P4XQ", false, 0, 0, m, sizeof(m));
    CHECK(strcmp(m, "LC-K7M2P4XQ Reporting at: no GPS fix") == 0);
    CHECK(okMessage("LC-K7M2P4XQ", true, 1, 1, m, 10) == 0);          // does not fit
    CHECK(strlen(m) <= 9);
  }
  { // journal lines: valid JSON text with hostile input
    char out[CAIRN_LINE];
    const char* nasty = "He said \"stop\" \\ and\nnew\tline \x01 caf\xC3\xA9";
    size_t n = recMsg(out, sizeof(out), 1770000000, 12, 'c', "Ann \"A\"", "Public", -3.5f, 2, nasty);
    CHECK(n > 0 && n == strlen(out));
    CHECK(strstr(out, "\"r\":\"m\"") && strstr(out, "\"k\":\"c\"") && strstr(out, "\"s\":-3.5") && strstr(out, "\"h\":2"));
    CHECK(strstr(out, "\\\"stop\\\"") && strstr(out, "\\\\") && strstr(out, "\\n") && strstr(out, "\\t"));
    CHECK(!strchr(out, '\n') && !strchr(out, '\x01'));
    CHECK(strstr(out, "caf\xC3\xA9"));
    emit(out);
    // a full-size message of the worst case (all quotes) is either kept whole or refused, never cut in half
    char big[200]; memset(big, '"', 160); big[160] = 0;
    n = recMsg(out, sizeof(out), 1, 1, 'd', "x", "", 0, -1, big);
    CHECK(n == 0 || out[n - 1] == '}');
    char med[200]; memset(med, 'a', 160); med[160] = 0;
    n = recMsg(out, sizeof(out), 1, 1, 'd', "x", "", 0, -1, med);
    CHECK(n > 0 && out[n - 1] == '}'); emit(out);
    n = recMsg(out, sizeof(out), 1, 1, 'r', "Room \xF0\x9F\x8F\xA0", "", 1.25f, -1, "hello"); CHECK(n > 0); emit(out);

    uint8_t key[6] = {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45};
    n = recAdv(out, sizeof(out), 1770000000, 99, 2, "Hill Top RPT", key, 0, 6.25f, -97, true, 29760430, -95369800, true, 29761000, -95370000);
    CHECK(n > 0 && strstr(out, "\"k\":\"ABCDEF012345\"") && strstr(out, "\"la\":29.76043") && strstr(out, "\"lo\":-95.36980") && strstr(out, "\"hla\":29.76100"));
    emit(out);
    n = recAdv(out, sizeof(out), 5, 5, 1, "", key, 3, -10.0f, -120, false, 0, 0, false, 0, 0);
    CHECK(n > 0 && !strstr(out, "\"la\"") && !strstr(out, "\"hla\"")); emit(out);
    n = recOwn(out, sizeof(out), 1770000100, 130, "LC-K7M2P4XQ", 0, "LC-K7M2P4XQ Reporting at: 29.76043,-95.36980"); CHECK(n > 0); emit(out);
    n = recBoot(out, sizeof(out), 1770000000, "LC-K7M2P4XQ", CAIRN_VERSION); CHECK(n > 0 && strstr(out, "\"u\":0")); emit(out);
    char tiny[20]; CHECK(recMsg(tiny, sizeof(tiny), 1, 1, 'c', "a", "b", 0, 0, "hello") == 0);
  }
  { // the ring: two readers, an overflow, and over-long lines
    Journal j; Cursor a, b;
    CHECK(j.peek(a) == nullptr);
    j.push("one"); j.push("two", true);
    CHECK(j.urgent()); j.clearUrgent(); CHECK(!j.urgent());
    CHECK(strcmp(j.peek(a), "one") == 0); j.advance(a); CHECK(strcmp(j.peek(a), "two") == 0);
    CHECK(strcmp(j.peek(b), "one") == 0);
    CHECK(j.pending(a) == 1 && j.pending(b) == 2);
    j.advance(a); CHECK(j.peek(a) == nullptr); j.advance(a); CHECK(j.pending(a) == 0);
    for (int i = 0; i < CAIRN_RING + 5; i++) { char s[16]; snprintf(s, sizeof(s), "L%d", i); j.push(s); }
    const char* f = j.peek(b);          // b was behind: it skips the lines that were overwritten
    CHECK(f != nullptr && b.lost == 7);   // 31 lines written, ring holds 24
    CHECK(j.pending(b) == CAIRN_RING);
    char longline[CAIRN_LINE + 10]; memset(longline, 'x', sizeof(longline)); longline[sizeof(longline) - 1] = 0;
    uint32_t before = j.total; CHECK(!j.push(longline)); CHECK(j.total == before && j.refused == 1);
    CHECK(!j.push("")); CHECK(!j.push(nullptr));
  }
  { // advert de-duplication
    AdvertSeen s; uint8_t k1[6] = {1, 2, 3, 4, 5, 6}, k2[6] = {9, 9, 9, 9, 9, 9};
    CHECK(s.shouldLog(k1, 1000, 3));
    CHECK(!s.shouldLog(k1, 1000, 3));      // same advert, same route
    CHECK(!s.shouldLog(k1, 1000, 5));      // same advert, longer route
    CHECK(s.shouldLog(k1, 1000, 1));       // same advert, shorter route: worth a line
    CHECK(!s.shouldLog(k1, 1000, 1));
    CHECK(s.shouldLog(k1, 2000, 3));       // a newer advert from the same node
    CHECK(s.shouldLog(k2, 1000, 3));
    for (int i = 0; i < 100; i++) { uint8_t k[6] = {(uint8_t)i, 0, 0, 0, 0, 7}; CHECK(s.shouldLog(k, 5, 1)); }   // table wraps without trouble
  }
  { // commands
    Cairn c; char r[200];
    c.runCommand("", r, sizeof(r), 125); CHECK(strstr(r, "LODESTAR CAIRN") && strstr(r, "(none)") && strstr(r, "up 2 min"));
    c.setId("LC-K7M2P4XQ"); c.setId("bad"); CHECK(strcmp(c.id, "LC-K7M2P4XQ") == 0 && c.hasId());
    c.file_bytes = 20480; c.runCommand(" status", r, sizeof(r), 0); CHECK(strstr(r, "LC-K7M2P4XQ") && strstr(r, "20 KB"));
    c.runCommand(" id", r, sizeof(r), 0); CHECK(strcmp(r, "LC-K7M2P4XQ") == 0);
    c.runCommand(" say 0 Water at Elm St", r, sizeof(r), 0); CHECK(c.want_say && c.say_channel == 0 && strcmp(c.say_text, "Water at Elm St") == 0 && strstr(r, "OK"));
    c.want_say = false;
    c.runCommand(" say 7   spaced  text", r, sizeof(r), 0); CHECK(c.want_say && c.say_channel == 7 && strcmp(c.say_text, "spaced  text") == 0); c.want_say = false;
    c.runCommand(" say 40 hi", r, sizeof(r), 0); CHECK(!c.want_say && strstr(r, "Err"));
    c.runCommand(" say x hi", r, sizeof(r), 0); CHECK(!c.want_say && strstr(r, "Err"));
    c.runCommand(" say 0", r, sizeof(r), 0); CHECK(!c.want_say && strstr(r, "Err"));
    { std::string longm(" say 0 "); longm += std::string(CAIRN_SAY_MAX + 1, 'x'); c.runCommand(longm.c_str(), r, sizeof(r), 0); CHECK(!c.want_say && strstr(r, "too long")); }
    { std::string okm(" say 0 "); okm += std::string(CAIRN_SAY_MAX, 'y'); c.runCommand(okm.c_str(), r, sizeof(r), 0); CHECK(c.want_say && strlen(c.say_text) == CAIRN_SAY_MAX); c.want_say = false; }
    c.runCommand(" dm A1B2c3D4E5F6 hello there", r, sizeof(r), 0);
    CHECK(c.want_dm && c.dm_key[0] == 0xA1 && c.dm_key[1] == 0xB2 && c.dm_key[2] == 0xC3 && c.dm_key[5] == 0xF6 && strcmp(c.say_text, "hello there") == 0); c.want_dm = false;
    c.runCommand(" dm A1B2C3 hi", r, sizeof(r), 0); CHECK(!c.want_dm && strstr(r, "Err"));
    c.runCommand(" dm A1B2C3D4E5G6 hi", r, sizeof(r), 0); CHECK(!c.want_dm && strstr(r, "Err"));
    c.want_say = true; c.runCommand(" say 0 second", r, sizeof(r), 0); CHECK(strstr(r, "still sending")); c.want_say = false;
    c.runCommand(" chans", r, sizeof(r), 0); CHECK(c.want_chans); c.want_chans = false;
    c.runCommand(" ok", r, sizeof(r), 0); CHECK(c.want_ok && c.ok_channel == 0 && strstr(r, "OK"));
    c.want_ok = false; c.runCommand(" ok 3", r, sizeof(r), 0); CHECK(c.want_ok && c.ok_channel == 3);
    c.want_ok = false; c.runCommand(" ok 99", r, sizeof(r), 0); CHECK(!c.want_ok && strstr(r, "Err"));
    c.runCommand(" ok x", r, sizeof(r), 0); CHECK(!c.want_ok && strstr(r, "Err"));
    c.runCommand(" dump", r, sizeof(r), 0); CHECK(c.want_dump);
    c.runCommand(" clear", r, sizeof(r), 0); CHECK(!c.want_clear && strstr(r, "cairn clear yes"));
    c.runCommand(" clear yes", r, sizeof(r), 0); CHECK(c.want_clear);
    c.runCommand(" mode disaster", r, sizeof(r), 0); CHECK(c.want_mode == 1);
    c.runCommand(" mode normal", r, sizeof(r), 0); CHECK(c.want_mode == 2);
    c.want_mode = 0; c.runCommand(" mode purple", r, sizeof(r), 0); CHECK(c.want_mode == 0);
    c.runCommand(" time 1770000000", r, sizeof(r), 0); CHECK(c.want_time == 1770000000UL && strstr(r, "OK"));
    c.want_time = 0; c.runCommand(" time 12", r, sizeof(r), 0); CHECK(c.want_time == 0 && strstr(r, "Err"));
    c.runCommand(" time abc", r, sizeof(r), 0); CHECK(c.want_time == 0 && strstr(r, "Err"));
    c.runCommand(" time", r, sizeof(r), 0); CHECK(c.want_time == 0 && strstr(r, "Err"));
    c.runCommand(" timeout", r, sizeof(r), 0); CHECK(c.want_time == 0 && strstr(r, "cairn ["));
    c.runCommand(" idle", r, sizeof(r), 0); CHECK(strstr(r, "cairn [status"));   // "idle" is not "id"
    c.runCommand(" okay", r, sizeof(r), 0); CHECK(strstr(r, "cairn [status"));   // "okay" is not "ok"
  }

  { // sha256 and hashtag channel keys
    uint8_t h[32]; char hx[65];
    auto hexs = [&](const char* m, size_t n) { sha256((const uint8_t*)m, n, h); for (int i = 0; i < 32; i++) snprintf(hx + i * 2, 3, "%02x", h[i]); return std::string(hx); };
    CHECK(hexs("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hexs("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::string a100(100, 'a'); CHECK(hexs(a100.c_str(), 100) == "2816597888e4a0d3a36b82b83316ab32680eb8f00f8cd3b904d681246d285a0e");
    std::string a55(55, 'a'), a56(56, 'a'), a64(64, 'a');     // padding edge cases around the block size
    CHECK(hexs(a55.c_str(), 55) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(hexs(a56.c_str(), 56) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(hexs(a64.c_str(), 64) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    uint8_t k[16]; char kh[33]; hashtagKey("#test", k); hex16(k, kh); CHECK(strcmp(kh, "9cd8fcf22a47333b591d96a2b848b73f") == 0);
    char nrm[40];
    CHECK(hashtagNormalize("#Water-Tank", nrm, sizeof(nrm)) && strcmp(nrm, "#water-tank") == 0);
    CHECK(!hashtagNormalize("#bad name", nrm, sizeof(nrm))); CHECK(!hashtagNormalize("#", nrm, sizeof(nrm))); CHECK(!hashtagNormalize("water", nrm, sizeof(nrm)));
    CHECK(!hashtagNormalize("#caf\xC3\xA9", nrm, sizeof(nrm)));
    CHECK(strcmp(chanKind("Public", PUBLIC_KEY16), "pub") == 0);
    CHECK(strcmp(chanKind("#test", k), "tag") == 0); CHECK(strcmp(chanKind("#other", k), "key") == 0); CHECK(strcmp(chanKind("Family", k), "key") == 0);
    uint8_t p2[16]; CHECK(parseHex16("9CD8fcf22a47333b591d96a2b848b73f", p2) && memcmp(p2, k, 16) == 0);
    CHECK(!parseHex16("9cd8fcf22a47333b591d96a2b848b73", p2)); CHECK(!parseHex16("zzd8fcf22a47333b591d96a2b848b73f", p2));
    CHECK(chanNameOk("Family") && chanNameOk("Elm St 3")); CHECK(!chanNameOk("")); CHECK(!chanNameOk(" x")); CHECK(!chanNameOk("a\"b")); CHECK(!chanNameOk("a\\b")); CHECK(!chanNameOk("a\nb"));
    CHECK(!chanNameOk("0123456789012345678901234567890x"));   // 32 bytes does not fit
  }
  { // power plan
    using namespace power;
    CHECK(estDbm(22) == 28 && estDbm(10) == 22 && estDbm(-9) == 3);
    CHECK(milliwatts(28) == 631 && milliwatts(22) == 158 && milliwatts(30) == 1000);
    CHECK(legalMax(910.525f, 20, 22) == 22);      // 2 dBi antenna: 29 dBm worst case <= 30
    CHECK(legalMax(910.525f, 60, 22) == 22);      // 6 dBi is the allowed antenna
    CHECK(legalMax(910.525f, 90, 22) == 14);      // 9 dBi: limit 27 dBm; (s+12+1) <= 27 -> setting 14
    CHECK(legalMax(868.0f, 20, 22) == -100);      // not the US band: leave it alone
    CHECK(legalMax(910.525f, 20, 10) == 10);      // a board whose maximum is lower
  }
  { // config
    Config c; char b[100]; Config d;
    c.mode = 1; c.ant10 = 35; c.auto_min = 90; c.beacon_min = 7; c.help_on = 1; c.main_chan = 3;
    size_t n = saveConfig(c, b, sizeof(b)); CHECK(n > 0 && loadConfig(b, d));
    CHECK(d.mode == 1 && d.ant10 == 35 && d.auto_min == 90 && d.beacon_min == 7 && d.help_on == 1 && d.main_chan == 3 && d.eco == 1);
    Config e; CHECK(!loadConfig("CAIRN2 5 0 20 0 5 0 1 1 0", e)); CHECK(!loadConfig("CAIRN2 0 0 20 0 0 0 1 1 0", e)); CHECK(!loadConfig("garbage", e)); CHECK(!loadConfig(nullptr, e));
    CHECK(e.mode == 0 && e.beacon_min == 5);
    CHECK(saveConfig(c, b, 10) == 0);
  }
  { // messages
    char m[100];
    CHECK(helpMessage("LC-K7M2P4XQ", true, 29760430, -95369800, m, sizeof(m)) > 0 && strcmp(m, "LC-K7M2P4XQ NEEDS HELP at: 29.76043,-95.36980") == 0);
    CHECK(statusMessage("LC-K7M2P4XQ", 1, false, 0, 0, m, sizeof(m)) > 0 && strcmp(m, "LC-K7M2P4XQ STATUS NEED MEDICAL at: no GPS fix") == 0);
    CHECK(statusMessage("LC-K7M2P4XQ", 0, false, 0, 0, m, sizeof(m)) == 0); CHECK(statusMessage("LC-K7M2P4XQ", CAIRN_NSTATUS + 1, false, 0, 0, m, sizeof(m)) == 0);
    CHECK(markMessage("LC-K7M2P4XQ", 1, true, 1000000, 2000000, m, sizeof(m)) > 0 && strstr(m, "MARK WATER at: 1.00000,2.00000"));
    CHECK(markMessage("LC-K7M2P4XQ", CAIRN_NMARK + 1, true, 0, 0, m, sizeof(m)) == 0);
    CHECK(lowBatMessage("LC-K7M2P4XQ", 12, true, 1000000, 2000000, m, sizeof(m)) > 0 && strstr(m, "LOW BATTERY 12%"));
    for (int i = 1; i <= CAIRN_NSTATUS; i++) CHECK(statusMessage("LC-K7M2P4XQ", i, true, 89999999, -179999999, m, sizeof(m)) > 0 && strlen(m) <= 120);
  }
  { // commands: mode, power, help, auto, eco, channels
    Cairn c; char r[400]; c.setId("LC-K7M2P4XQ"); c.ctx.freq_mhz = 910.525f; c.ctx.hw_max = 22; c.ctx.setting = 22;
    CHECK(c.wantedSetting() == 22);                                   // Standard: highest the rules allow
    c.runCommand(" mode disaster", r, sizeof(r), 0); CHECK(c.disaster() && c.want_power && c.cfg_dirty && c.wantedSetting() == 22);
    c.runCommand(" mode standard", r, sizeof(r), 0); CHECK(!c.disaster());
    c.runCommand(" power saver", r, sizeof(r), 0); CHECK(c.wantedSetting() == 10 && strstr(r, "setting 10"));
    c.runCommand(" mode disaster", r, sizeof(r), 0); CHECK(c.wantedSetting() == 22);     // Disaster ignores the saver
    c.runCommand(" mode standard", r, sizeof(r), 0); c.runCommand(" power full", r, sizeof(r), 0); CHECK(c.wantedSetting() == 22);
    c.runCommand(" antenna 9", r, sizeof(r), 0); CHECK(c.cfg.ant10 == 90 && c.wantedSetting() == legalExpect9());
    c.runCommand(" antenna 30", r, sizeof(r), 0); CHECK(c.cfg.ant10 == 90 && strstr(r, "Err"));
    c.runCommand(" antenna 2", r, sizeof(r), 0); CHECK(c.wantedSetting() == 22);
    c.ctx.freq_mhz = 869.525f; CHECK(c.wantedSetting() == -100); c.ctx.freq_mhz = 910.525f;
    // battery saving
    c.ctx.batt_pct = 40; c.updateEco(); CHECK(!c.eco_low); c.ctx.batt_pct = 25; c.updateEco(); CHECK(c.eco_low && c.wantedSetting() == 10);
    c.ctx.batt_pct = 30; c.updateEco(); CHECK(c.eco_low); c.ctx.batt_pct = 35; c.updateEco(); CHECK(!c.eco_low && c.wantedSetting() == 22);
    c.ctx.batt_pct = 20; c.updateEco(); c.ctx.external = true; c.updateEco(); CHECK(!c.eco_low);
    c.ctx.external = false; c.ctx.batt_pct = 10; c.updateEco(); c.runCommand(" mode disaster", r, sizeof(r), 0); CHECK(c.wantedSetting() == 22);   // Disaster: full power even on a low battery
    c.runCommand(" mode standard", r, sizeof(r), 0);
    // low-battery message once
    c.ctx.batt_pct = 16; CHECK(!c.lowBatDue()); c.ctx.batt_pct = 15; CHECK(c.lowBatDue()); CHECK(!c.lowBatDue()); c.ctx.batt_pct = 10; CHECK(!c.lowBatDue());
    c.ctx.batt_pct = 31; CHECK(!c.lowBatDue()); c.ctx.batt_pct = 14; CHECK(c.lowBatDue());
    c.ctx.batt_pct = -1; CHECK(!c.lowBatDue());
    // help beacon
    c.ctx.batt_pct = 80; c.updateEco(); c.ctx.external = false;
    c.runCommand(" help", r, sizeof(r), 100); CHECK(c.cfg.help_on && c.want_help_now && c.wantedSetting() == 22 && strstr(r, "HELP beacon ON"));
    CHECK(c.helpDue(100)); c.want_help_now = false; c.helpSent(100); CHECK(!c.helpDue(100) && !c.helpDue(399) && c.helpDue(400) && c.helpIn(100) == 300);
    c.runCommand(" beacon 2", r, sizeof(r), 0); CHECK(c.cfg.beacon_min == 2); c.runCommand(" beacon 0", r, sizeof(r), 0); CHECK(c.cfg.beacon_min == 2 && strstr(r, "beacon every"));
    c.runCommand(" help off", r, sizeof(r), 500); CHECK(!c.cfg.help_on && c.want_help_stop); c.want_help_stop = false;
    c.runCommand(" help off", r, sizeof(r), 500); CHECK(!c.want_help_stop && strstr(r, "not running"));
    c.runCommand(" help 4", r, sizeof(r), 0); CHECK(c.helpChannel() == 4 && c.msgChannel() == 0); c.runCommand(" help off", r, sizeof(r), 0);
    c.runCommand(" help 99", r, sizeof(r), 0); CHECK(strstr(r, "cairn help"));
    // wrap-safe timers
    { Cairn w; w.cfg.help_on = 1; w.helpSent(0xFFFFFFF0u); CHECK(w.help_next_up == 0xFFFFFFF0u + 300u && !w.helpDue(0xFFFFFFF1u) && w.helpDue(0xFFFFFFF0u + 300u)); }
    // automatic check-in
    c.cfg.help_on = 0; c.runCommand(" auto 30", r, sizeof(r), 1000); CHECK(c.cfg.auto_min == 30 && c.auto_next_up == 1000 + 1800 && !c.autoDue(2000) && c.autoDue(2800));
    c.eco_low = true; c.autoReset(1000); CHECK(c.auto_next_up == 1000 + 3600); c.eco_low = false;
    c.cfg.help_on = 1; CHECK(!c.autoDue(99999)); c.cfg.help_on = 0;
    c.runCommand(" auto 5", r, sizeof(r), 0); CHECK(c.cfg.auto_min == 30 && strstr(r, "Err")); c.runCommand(" auto off", r, sizeof(r), 0); CHECK(c.cfg.auto_min == 0 && !c.autoDue(99999));
    c.runCommand(" eco off", r, sizeof(r), 0); CHECK(!c.cfg.eco); c.ctx.batt_pct = 5; c.updateEco(); CHECK(!c.eco_low); c.runCommand(" eco on", r, sizeof(r), 0); CHECK(c.cfg.eco);
    c.runCommand(" main 5", r, sizeof(r), 0); CHECK(c.cfg.main_chan == 5 && c.msgChannel() == 5); c.runCommand(" main 40", r, sizeof(r), 0); CHECK(c.cfg.main_chan == 5);
    c.runCommand(" ok", r, sizeof(r), 0); CHECK(c.want_ok && c.ok_channel == 5); c.want_ok = false;
    // status and marks
    c.runCommand(" ustatus 2", r, sizeof(r), 0); CHECK(c.want_status == 2 && c.msgChannel() == 5 && strstr(r, "NEED WATER")); c.want_status = 0;
    c.runCommand(" ustatus 3 7", r, sizeof(r), 0); CHECK(c.want_status == 3 && c.msgChannel() == 7); c.want_status = 0; c.msg_channel = -1;
    c.runCommand(" ustatus 9", r, sizeof(r), 0); CHECK(c.want_status == 0 && strstr(r, "Err"));
    c.runCommand(" mark 4", r, sizeof(r), 0); CHECK(c.want_mark == 4 && strstr(r, "HAZARD")); c.want_mark = 0; c.runCommand(" mark 0", r, sizeof(r), 0); CHECK(c.want_mark == 0);
    c.runCommand(" config", r, sizeof(r), 0); CHECK(strstr(r, "standard") && strstr(r, "main channel 5"));
    // channels
    c.runCommand(" chan add #Water", r, sizeof(r), 0); CHECK(c.chan_op == 1 && strcmp(c.chan_name, "#water") == 0 && c.chan_has_key);
    { uint8_t k[16]; hashtagKey("#water", k); CHECK(memcmp(k, c.chan_key, 16) == 0); }
    c.chan_op = 0; c.runCommand(" chan add #bad name", r, sizeof(r), 0); CHECK(c.chan_op == 0 && strstr(r, "Err"));
    c.runCommand(" chan add Family 00112233445566778899aabbccddeeff", r, sizeof(r), 0); CHECK(c.chan_op == 1 && strcmp(c.chan_name, "Family") == 0 && c.chan_key[0] == 0x00 && c.chan_key[15] == 0xff);
    c.chan_op = 0; c.runCommand(" chan add Block Watch 00112233445566778899aabbccddeeff", r, sizeof(r), 0); CHECK(c.chan_op == 1 && strcmp(c.chan_name, "Block Watch") == 0); c.chan_op = 0;
    c.runCommand(" chan add Family", r, sizeof(r), 0); CHECK(c.chan_op == 0 && strstr(r, "needs a key"));
    c.runCommand(" chan new Family", r, sizeof(r), 0); CHECK(c.chan_op == 2 && !c.chan_has_key && strcmp(c.chan_name, "Family") == 0);
    c.runCommand(" chan remove 2", r, sizeof(r), 0); CHECK(c.chan_op == 2);   // busy: refuses while a change is pending
    c.chan_op = 0; c.runCommand(" chan remove 2", r, sizeof(r), 0); CHECK(c.chan_op == 3 && c.chan_idx == 2);
    c.chan_op = 0; c.runCommand(" chan remove 0", r, sizeof(r), 0); CHECK(c.chan_op == 0 && strstr(r, "cannot be removed"));
    c.runCommand(" chan remove 40", r, sizeof(r), 0); CHECK(c.chan_op == 0);
    c.runCommand(" chan key 3", r, sizeof(r), 0); CHECK(c.chan_op == 4 && c.chan_idx == 3); c.chan_op = 0;
    c.runCommand(" chan add a\"b 00112233445566778899aabbccddeeff", r, sizeof(r), 0); CHECK(c.chan_op == 0 && strstr(r, "Err"));
    c.runCommand(" chan", r, sizeof(r), 0); CHECK(c.want_chans); c.want_chans = false;
  }
  { // status JSON
    Cairn c; c.setId("LC-K7M2P4XQ"); c.ctx.freq_mhz = 910.525f; c.ctx.hw_max = 22; c.ctx.setting = 22; c.ctx.batt_pct = 80; c.file_bytes = 4096;
    char o[700]; size_t n = c.statusJson(o, sizeof(o), 125, 1770000000, 0);
    CHECK(n > 0 && n == strlen(o) && o[0] == '{' && o[n - 1] == '}' && !strchr(o, '\n'));
    CHECK(strstr(o, "\"tx\":28") && strstr(o, "\"mw\":631") && strstr(o, "\"mode\":\"standard\"") && strstr(o, "\"bat\":80") && strstr(o, "\"ant\":2.0") && strstr(o, "\"legal\":22"));
    CHECK(c.statusJson(o, 50, 0, 0, 0) == 0);
    FILE* f = fopen("/tmp/status.json", "w"); if (f) { c.statusJson(o, sizeof(o), 125, 1770000000, 0); fputs(o, f); fclose(f); }
  }
  if (jl) fclose(jl);
  if (fails) { printf("%d FAILED\n", fails); return 1; }
  printf("all tests passed\n");
  return 0;
}
