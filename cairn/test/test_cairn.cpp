// Host-side tests for Cairn.h:  g++ -std=c++17 -Wall -Wextra -Icairn cairn/test/test_cairn.cpp -o t && ./t [lines.jsonl]
// With a file name, the test also writes every journal line it makes there, so the build can check they are real JSON.
#include <stdio.h>
#include <string.h>
#include "Cairn.h"
using namespace cairn;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

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
  if (jl) fclose(jl);
  if (fails) { printf("%d FAILED\n", fails); return 1; }
  printf("all tests passed\n");
  return 0;
}
