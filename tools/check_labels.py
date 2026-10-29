#!/usr/bin/env python3
"""The status and mark labels live in cairn/Cairn.h (what the radio sends) and in dashboard/cairn.html (what the page reads).
They must match exactly or the page will not understand the messages. This checks they do."""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
h = (root / "cairn" / "Cairn.h").read_text(encoding="utf-8")
d = (root / "dashboard" / "cairn.html").read_text(encoding="utf-8")
def arr(text, pat):
    m = re.search(pat, text, re.S)
    if not m: sys.exit("not found: " + pat)
    return re.findall(r'"([^"]*)"', m.group(1))
hs = arr(h, r"STATUS_LABEL\[[^\]]*\]\s*=\s*\{(.*?)\}"); hm = arr(h, r"MARK_LABEL\[[^\]]*\]\s*=\s*\{(.*?)\}")
ds = arr(d, r"const STATUS_LABEL = \[(.*?)\]"); dm = arr(d, r"const MARK_LABEL = \[(.*?)\]")
ok = True
if hs != ds: print("STATUS labels differ:", hs, ds); ok = False
if hm != dm: print("MARK labels differ:", hm, dm); ok = False
ver_h = re.search(r'CAIRN_VERSION\s+"([^"]+)"', h).group(1); ver_d = re.search(r'DASH_VER = "([^"]+)"', d).group(1)
if ver_h != ver_d: print("version differs:", ver_h, ver_d); ok = False
if not ok: sys.exit(1)
print("labels and version match:", len(hs) - 1, "statuses,", len(hm) - 1, "marks, version", ver_h)
