#!/usr/bin/env python3
"""Build the stand-alone field guide from dashboard/guide.html.

  python3 tools/build_guide.py          -> docs/CAIRN-Field-Guide.html  (CI checks this is up to date)
  python3 tools/build_guide.py --pdf    -> also docs/CAIRN-Field-Guide.pdf and docs/CAIRN-Quick-Start-Card.pdf (needs Playwright + Chromium)
"""
import pathlib, re, sys
root = pathlib.Path(__file__).resolve().parent.parent
g = (root / "dashboard" / "guide.html").read_text(encoding="utf-8")
g = re.sub(r'<div class="g-btns">.*?</div>', "", g, count=1, flags=re.S)       # the print/download buttons only make sense inside the dashboard
page = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>CAIRN Field Guide - Wolf Reactive Systems (WRS)</title>
<style>:root{--ink:#111;--muted:#555;--accent:#0a6f8a;--line:#bbb;--good:#0b7a4f;--bad:#c4302b;--warn:#a15f00;--panel:#fff;--panel2:#f1f4f2}
body{margin:0;padding:18px;background:#fff;color:#111;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
@page{size:Letter;margin:14mm}</style></head><body>
""" + g + "\n</body></html>\n"
out = root / "docs" / "CAIRN-Field-Guide.html"
out.write_text(page, encoding="utf-8")
print(out, len(page), "bytes")
if "--pdf" in sys.argv:
    from playwright.sync_api import sync_playwright
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page()
        pg.goto(out.as_uri())
        pg.pdf(path=str(root / "docs" / "CAIRN-Field-Guide.pdf"), format="Letter", print_background=True, margin={"top": "14mm", "bottom": "16mm", "left": "14mm", "right": "14mm"},
               display_header_footer=True, header_template="<span></span>",
               footer_template='<div style="font-size:9px;width:100%;text-align:center;color:#555">LODESTAR CAIRN Field Guide v1.3.0 &middot; Wolf Reactive Systems (WRS) &middot; page <span class="pageNumber"></span> of <span class="totalPages"></span></div>')
        # one-page quick-start card
        pg.evaluate("""() => { const q = document.getElementById('g-quick'); const d = document.getElementById('guideDoc'); Array.from(d.children).forEach(c => { if (c !== q && c.tagName !== 'STYLE') c.remove(); }); q.style.pageBreakAfter = 'auto'; q.style.margin = '0'; const st = document.createElement('style'); st.textContent = '@media print { #guideDoc { font-size: 12pt !important; } #guideDoc ol.g-steps > li { margin: 7px 0; padding: 8px 10px 8px 62px; min-height: 0; } #guideDoc .g-qs { border-width: 3px; padding: 10px 16px; break-inside: avoid; page-break-inside: avoid; } #guideDoc .g-lead { font-size: 12pt; } #guideDoc .g-mut { font-size: 10pt; } #guideDoc .g-box b.t { font-size: 12pt; } #guideDoc h2 { font-size: 20pt; } #guideDoc p, #guideDoc li { margin: 5px 0; } }'; document.head.appendChild(st); }""")
        pg.pdf(path=str(root / "docs" / "CAIRN-Quick-Start-Card.pdf"), format="Letter", print_background=True, scale=0.82, margin={"top": "12mm", "bottom": "12mm", "left": "14mm", "right": "14mm"})
        b.close()
    print("PDFs written")
