#!/usr/bin/env python3
"""Assemble docs/index.html from dashboard/cairn.html + inlined libs (works with no internet)."""
import pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent
d = root / "dashboard"
src = (d / "cairn.html").read_text(encoding="utf-8")
libs = (d / "libs.html").read_text(encoding="utf-8")
css = (d / "leaflet.css").read_text(encoding="utf-8")
guide = (d / "guide.html").read_text(encoding="utf-8")
for mark in ("<!--LIBS-->", "/*LEAFLETCSS*/", "<!--GUIDE-->"):
    if src.count(mark) != 1:
        sys.exit("marker %s must appear exactly once" % mark)
out = src.replace("/*LEAFLETCSS*/", css).replace("<!--LIBS-->", libs).replace("<!--GUIDE-->", guide)
(root / "docs").mkdir(exist_ok=True)
(root / "docs" / "index.html").write_text(out, encoding="utf-8")
print("docs/index.html", len(out), "bytes")
