"""Drives the dashboard the way the Android app does: with a pretend tablet (window.CairnBridge) that answers
like the real one. Checks the start-up steps (Wi-Fi skippable; repeater and radio not), the links, Files, Online.
Run:  python3 dashboard/test/test_station.py      Needs: pip install playwright && playwright install chromium"""
import json, pathlib, sys
from playwright.sync_api import sync_playwright

page_url = (pathlib.Path(__file__).resolve().parents[2] / "docs" / "index.html").as_uri() + "?nosplash"
errs = []
def check(c, msg):
    if not c:
        print("FAIL:", msg); print("::error::station test: " + msg.replace("\n", " ")[:300]); errs.append(msg)

MOCK = r"""
(() => {
  const calls = []; window.__calls = calls;
  const net = { wifi: false, online: false }; window.__net = net;
  const files = [
    { path: "logs/journal-2026-10-28.log", kind: "logs", name: "journal-2026-10-28.log", size: 2048, mtime: Date.now() - 3600000 },
    { path: "downloads/cairn-station-1.3.0.apk", kind: "downloads", name: "cairn-station-1.3.0.apk", size: 5000000, mtime: Date.now() - 600000 },
  ]; window.__files = files;
  const cache = {};
  const emit = (t, o) => setTimeout(() => window.cairnNative && window.cairnNative.emit(t, JSON.stringify(o)), 20);
  window.__emit = emit;
  const soon = new Date(Date.now() + 3 * 3600000).toISOString();
  const body = (key) => ({
    wxpoint: { properties: { forecast: "https://api.weather.gov/gridpoints/HGX/1,1/forecast", relativeLocation: { properties: { city: "Houston", state: "TX" } } } },
    wxfc: { properties: { periods: [{ name: "Tonight", temperature: 68, temperatureUnit: "F", shortForecast: "Clear", windSpeed: "5 mph", windDirection: "N" }, { name: "Thursday", temperature: 85, temperatureUnit: "F", shortForecast: "Sunny", windSpeed: "10 mph", windDirection: "S" }] } },
    wxalerts: { features: [{ properties: { event: "Flood Watch", severity: "Moderate", headline: "Flood Watch until 6 PM", areaDesc: "Harris County", expires: soon, instruction: "Turn around, do not drown." } }] },
    quakes: { features: [{ properties: { mag: 2.6, place: "5 km N of Somewhere, TX", time: Date.now() - 3600000 } }] },
    releases: [{ tag_name: "cairn-alpha7", draft: false, published_at: "2026-10-29T00:00:00Z", assets: [{ name: "cairn-station-9.9.9.apk", size: 5000000, browser_download_url: "https://github.com/x/y/releases/download/cairn-alpha7/cairn-station-9.9.9.apk" }, { name: "firmware-merged.bin", size: 1700000, browser_download_url: "https://github.com/x/y/releases/download/cairn-alpha7/firmware-merged.bin" }] }],
  })[key];
  window.CairnBridge = {
    info: () => JSON.stringify({ app: "1.2.0", lastRadio: { addr: "", name: "" } }),
    snapshot: () => "{}",
    netNow: () => JSON.stringify(net),
    wifiSettings: () => calls.push(["wifi"]), bluetoothSettings: () => calls.push(["btsettings"]), androidSettings: () => {},
    bleAllowed: () => true, bleAsk: () => {},
    bleScan: (on) => { calls.push(["scan", on]); if (on) emit("bleDev", { addr: "AA:BB:CC:DD:EE:FF", name: "MeshCore-Test", rssi: -52, bonded: true }); return "ok"; },
    bleOpen: (a, n) => { calls.push(["bleopen", a]); emit("ble", { state: "connecting", msg: "Connecting to " + n, addr: a, name: n }); setTimeout(() => emit("ble", { state: "connected", msg: "Connected to " + n, addr: a, name: n }), 250); return "ok"; },
    bleClose: () => {},
    bleWrite: (l) => { calls.push(["ble", l]); if (l === "cairn id") emit("bleData", { t: '{"cairn":1,"id":"LC-K7M2P4XQ","now":' + Math.round(Date.now() / 1000) + '}' }); return "ok"; },
    usbList: () => JSON.stringify([{ id: "/dev/bus/usb/001/002", name: "XIAO ESP32S3 (303a:1001)", serial: "", perm: true, chosen: false }]),
    usbOpen: (id) => { calls.push(["usbopen", id]); emit("usb", { state: "open", msg: "Connected to XIAO ESP32S3", name: "XIAO" }); return "ok"; },
    usbClose: () => {},
    usbWrite: (t) => { calls.push(["usb", t]); if (t === "ver") emit("usbData", { t: "MeshCore repeater test v1" }); return "ok"; },
    log: (k, l) => calls.push(["log", k, l]),
    fileList: () => JSON.stringify(files),
    fileRead: (p) => "line one\nline two for " + p,
    fileWrite: (k, n, t) => { calls.push(["write", k, n]); files.push({ path: k + "/" + n, kind: k, name: n, size: t.length, mtime: Date.now() }); return k + "/" + n; },
    fileDelete: (p) => { calls.push(["delete", p]); const i = files.findIndex((f) => f.path === p); if (i >= 0) files.splice(i, 1); return true; },
    fileShare: (p) => { calls.push(["share", p]); return true; }, fileExport: (p) => { calls.push(["export", p]); return "Download/CAIRN/x"; }, installApk: (p) => { calls.push(["install", p]); return "ok"; },
    httpGet: (key, url) => { calls.push(["http", key, url]); cache[key] = JSON.stringify(body(key)); emit("http", { key, ok: true, status: 200, at: Date.now() }); return "ok"; },
    cached: (key) => cache[key] ? JSON.stringify({ at: Date.now(), body: cache[key] }) : "",
    download: (kind, name, url) => { calls.push(["download", kind, name]); return "ok"; }, downloadCancel: () => {},
    mapList: () => "[]", mapDelete: () => true,
    gemini: () => { calls.push(["gemini"]); return net.online ? "ok" : "offline"; },
    keepScreenOn: () => {},
  };
})();
"""

with sync_playwright() as p:
    b = p.chromium.launch()
    # the ordinary browser page must be unchanged: no start-up steps, no Files/Online tabs
    pg0 = b.new_page(viewport={"width": 1280, "height": 800}); pg0.goto(page_url); pg0.wait_for_timeout(400)
    check(pg0.locator("#gate").is_hidden(), "no start-up steps in a normal browser")
    check(pg0.locator("#tabs button[data-tab=files]").is_hidden() and pg0.locator("#tabs button[data-tab=online]").is_hidden(), "Files and Online tabs hidden in a normal browser")
    check(pg0.evaluate("!document.body.classList.contains('station')"), "not in station mode in a normal browser")
    pg0.close()

    for vp in ({"width": 1280, "height": 800}, {"width": 800, "height": 1280}):
        ctx = b.new_context(viewport=vp)
        ctx.add_init_script(MOCK)
        pg = ctx.new_page(); js = []
        pg.on("pageerror", lambda e: js.append(str(e)))
        pg.on("dialog", lambda d: d.accept())
        pg.goto(page_url); pg.wait_for_timeout(700)
        tag = "%dx%d: " % (vp["width"], vp["height"])
        check(pg.evaluate("document.body.classList.contains('station')"), tag + "station mode on")
        check(pg.locator("#gate").is_visible(), tag + "start-up steps show first")
        check("Wi-Fi" in pg.inner_text("#gTitle"), tag + "step 1 is Wi-Fi")
        check("skip" in pg.inner_text("#gBtns").lower(), tag + "Wi-Fi can be skipped when not connected")
        check(pg.locator("#btnGem").is_disabled(), tag + "Gemini is off with no internet")
        check(pg.evaluate("(() => { const r = document.querySelector('#gate .gbox').getBoundingClientRect(); return r.left >= 0 && r.right <= innerWidth + 1; })()"), tag + "steps fit the screen")
        # Wi-Fi skipped -> repeater step, which cannot be skipped
        pg.click("#gBtns [data-act=next]"); pg.wait_for_timeout(200)
        check("Repeater" in pg.inner_text("#gTitle"), tag + "step 2 is the repeater")
        check(pg.locator("#gBtns [data-act=next]").is_disabled(), tag + "repeater step cannot be skipped")
        check("XIAO" in pg.inner_text("#gBody"), tag + "USB device listed")
        pg.click("#gBody [data-act=usb]"); pg.wait_for_selector("#gBtns [data-act=next]:not([disabled])", timeout=8000)
        check(not pg.locator("#gBtns [data-act=next]").is_disabled(), tag + "Continue works once the repeater is connected")
        pg.click("#gBtns [data-act=next]"); pg.wait_for_timeout(300)
        check("Companion" in pg.inner_text("#gTitle"), tag + "step 3 is the companion radio")
        check(pg.locator("#gBtns [data-act=next]").is_disabled(), tag + "radio step cannot be skipped")
        check("MeshCore-Test" in pg.inner_text("#gBody"), tag + "radio found by the scan")
        pg.click("#gBody [data-act=ble]"); pg.wait_for_selector("#gBtns [data-act=next]:not([disabled])", timeout=8000)
        check(not pg.locator("#gBtns [data-act=next]").is_disabled(), tag + "Open the dashboard works once the radio is connected")
        pg.click("#gBtns [data-act=next]"); pg.wait_for_selector("#gate", state="hidden", timeout=8000)
        check(pg.locator("#gate").is_hidden(), tag + "dashboard opens")
        pg.wait_for_function("document.getElementById('sid').textContent.indexOf('LC-K7M2P4XQ') >= 0", timeout=8000)
        calls = pg.evaluate("window.__calls")
        sent = [c[1] for c in calls if c[0] == "ble"]
        check("nova hello" in sent and "cairn id" in sent and "cairn dump" in sent, tag + "dashboard asks the radio for its data over Bluetooth: %s" % sent[:6])
        check("LC-K7M2P4XQ" in pg.inner_text("#sid"), tag + "station ID shown from the radio's reply")
        check(pg.evaluate("document.getElementById('lnkRadio').dataset.s") == "live" and pg.evaluate("document.getElementById('lnkRep').dataset.s") == "live", tag + "radio and repeater chips are green")
        # repeater text
        try: pg.wait_for_function("window.__calls.some(c => c[0] === 'usb' && c[1] === 'ver')", timeout=8000)
        except Exception: pass
        usb = [c[1] for c in pg.evaluate("window.__calls") if c[0] == "usb"]
        check("ver" in usb, tag + "repeater is asked for its version")
        pg.click("#btnView"); pg.click("#tabs button[data-tab=station]"); pg.wait_for_timeout(300)
        try: pg.wait_for_function("document.getElementById('repOut').innerText.indexOf('MeshCore repeater test v1') >= 0", timeout=8000)
        except Exception: pass
        check("MeshCore repeater test v1" in pg.inner_text("#repOut"), tag + "repeater text shows on the Station screen")
        pg.fill("#repIn", "neighbors"); pg.press("#repIn", "Enter"); pg.wait_for_timeout(200)
        check(["usb", "neighbors"] in pg.evaluate("window.__calls"), tag + "a command can be sent to the repeater")
        check(any(c[0] == "log" and c[1] == "repeater" and "MeshCore repeater test v1" in c[2] for c in pg.evaluate("window.__calls")), tag + "repeater text is logged")
        # journal lines are logged once the radio has finished sending its stored journal
        pg.evaluate('window.__emit("bleData", {t: "{\\"cairn\\":1,\\"dump\\":\\"end\\",\\"lines\\":0}"})'); pg.wait_for_timeout(200)
        pg.evaluate('window.__emit("bleData", {t: "{\\"r\\":\\"b\\",\\"t\\":1770000000,\\"u\\":0,\\"id\\":\\"LC-K7M2P4XQ\\",\\"v\\":\\"1.2.0\\"}"})'); pg.wait_for_timeout(200)
        check(any(c[0] == "log" and c[1] == "journal" for c in pg.evaluate("window.__calls")), tag + "new journal lines are logged on the tablet")
        # Files
        check(pg.locator("#tabs button[data-tab=files]").is_visible() and pg.locator("#tabs button[data-tab=online]").is_visible(), tag + "Files and Online tabs show")
        pg.click("#tabs button[data-tab=files]"); pg.wait_for_timeout(300)
        check("journal-2026-10-28.log" in pg.inner_text("#fBody"), tag + "files listed")
        pg.click("#fBody [data-fa=view]"); pg.wait_for_timeout(200)
        check("line one" in pg.inner_text("#fView"), tag + "a log can be read on screen")
        check(pg.locator("#fBody [data-fa=install]").count() == 1, tag + "an app package offers Install")
        pg.click("#fBody tr:nth-child(1) [data-fa=del]"); pg.wait_for_timeout(300)
        check(any(c[0] == "delete" for c in pg.evaluate("window.__calls")), tag + "a file can be deleted")
        pg.click("#fCsv"); pg.wait_for_timeout(300)
        check(any(c[0] == "write" and c[1] == "reports" and c[2].endswith(".csv") for c in pg.evaluate("window.__calls")), tag + "journal CSV is saved into Files")
        # Online: no internet yet
        pg.click("#tabs button[data-tab=online]"); pg.wait_for_timeout(300)
        check(pg.locator("#onSync").is_disabled(), tag + "fetching is off with no internet")
        pg.fill("#wxPos", "29.76, -95.37")
        pg.evaluate("window.__net.wifi = true; window.__net.online = true; window.__emit('net', window.__net)")
        pg.wait_for_function("document.getElementById('upList').innerText.indexOf('newer tablet app') >= 0", timeout=15000)
        check(not pg.locator("#btnGem").is_disabled(), tag + "Gemini turns on with internet")
        check("Flood Watch" in pg.inner_text("#wxAlerts"), tag + "weather alert shown")
        check("Houston" in pg.inner_text("#wxWhere"), tag + "weather place shown")
        check("Tonight" in pg.inner_text("#wxFc"), tag + "forecast shown")
        check(pg.locator("#lnkWx").is_visible(), tag + "alert chip in the header")
        check("M2.6" in pg.inner_text("#eqList"), tag + "earthquake list shown")
        check("newer tablet app" in pg.inner_text("#upList"), tag + "update noticed")
        pg.click("#upList [data-dl][data-n='cairn-station-9.9.9.apk']"); pg.wait_for_timeout(200)
        check(["download", "release", "cairn-station-9.9.9.apk"] in pg.evaluate("window.__calls"), tag + "update downloads through the tablet")
        pg.click("#btnGem"); pg.wait_for_timeout(200)
        check(["gemini"] in pg.evaluate("window.__calls"), tag + "Gemini opens")
        # internet lost: Gemini off, last answers stay
        pg.evaluate("window.__net.wifi = false; window.__net.online = false; window.__emit('net', window.__net)"); pg.wait_for_timeout(400)
        check(pg.locator("#btnGem").is_disabled(), tag + "Gemini turns off again")
        check("Flood Watch" in pg.inner_text("#wxAlerts"), tag + "last weather stays on screen offline")
        check(pg.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"), tag + "no sideways scroll on Online")
        # link lost -> steps come back by themselves
        if vp["width"] == 1280:
            pg.evaluate("window.__emit('ble', {state: 'closed', msg: 'The radio disconnected.', addr: '', name: ''})")
            pg.wait_for_selector("#gate", state="visible", timeout=25000)
            check(pg.locator("#gate").is_visible() and "Companion" in pg.inner_text("#gTitle"), tag + "steps return when the radio link is lost")
            check(pg.locator("#gBtns [data-act=next]").is_disabled(), tag + "and cannot be dismissed without the radio")
        check(not js, tag + "no page errors: %s" % js)
        ctx.close()
    b.close()
print("FAILED %d" % len(errs) if errs else "station tests passed")
sys.exit(1 if errs else 0)
