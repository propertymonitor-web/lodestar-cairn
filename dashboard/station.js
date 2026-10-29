/* ---------------------------------------------------------------- CAIRN STATION: the tablet app (Android). Does nothing in a normal browser. */
(function () {
  if (!STN) return;
  const ST = window.CairnBridge;
  const call = (fn, ...a) => { try { return ST[fn](...a); } catch (e) { return null; } };
  const jcall = (fn, ...a) => { try { const r = ST[fn](...a); return r ? JSON.parse(r) : null; } catch (e) { return null; } };
  const SX = { net: { wifi: false, online: false }, ble: { state: "idle", msg: "", name: "", addr: "" }, usb: { state: "idle", msg: "", name: "" },
    devs: new Map(), info: {}, scanNote: "", rep: [], repTimer: null, dl: {}, waiters: new Map(), syncAt: 0, badSince: 0, hadRadio: false };
  const G = { open: false, step: "wifi", held: false, timer: null, passedWifi: false, why: "" };
  const STEPS = [
    { id: "wifi", t: "1  Wi-Fi", s: "optional" },
    { id: "repeater", t: "2  Repeater", s: "USB cable - needed" },
    { id: "radio", t: "3  Companion radio", s: "Bluetooth - needed" },
  ];
  const fmtB = (b) => (b < 1024 ? b + " B" : b < 1048576 ? Math.round(b / 1024) + " KB" : (b / 1048576).toFixed(b < 10485760 ? 1 : 0) + " MB");
  const when = (ms) => new Date(ms).toLocaleString([], { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" });
  const ago = (ms) => { const m = Math.round((Date.now() - ms) / 60000); return m < 2 ? "just now" : m < 120 ? m + " min ago" : m < 2880 ? Math.round(m / 60) + " h ago" : Math.round(m / 1440) + " days ago"; };
  const jget = (k) => { try { return JSON.parse(store.get(k) || "null"); } catch (e) { return null; } };

  document.body.classList.add("station");
  $("btnConnect").textContent = "Connections";
  $("btnConnect").classList.remove("pri");
  $("status").insertAdjacentHTML("afterend",
    '<span class="chip lnk" id="lnkRadio" data-s="off" role="button" tabindex="0" title="Companion radio (Bluetooth)"><i></i>Radio</span>' +
    '<span class="chip lnk" id="lnkRep" data-s="off" role="button" tabindex="0" title="Repeater (USB cable)"><i></i>Repeater</span>' +
    '<span class="chip lnk" id="lnkWifi" data-s="off" role="button" tabindex="0" title="Wi-Fi"><i></i>Wi-Fi</span>' +
    '<span class="chip lnk" id="lnkWx" data-s="bad" role="button" tabindex="0" title="Official weather alert for this place" hidden><i></i><span id="lnkWxT">Weather alert</span></span>' +
    '<button id="btnGem" title="Gemini needs the internet">Ask Gemini</button>');
  ["lnkRadio", "lnkRep", "lnkWifi"].forEach((id) => $(id).addEventListener("click", () => openGate(id === "lnkWifi" ? "wifi" : id === "lnkRep" ? "repeater" : "radio", true)));
  $("lnkWx").addEventListener("click", () => showTab("online"));

  /* ---- the repeater card on the Station screen ---- */
  const rc = document.createElement("div");
  rc.className = "card span2"; rc.id = "repCard";
  rc.innerHTML = '<h2>Repeater (USB cable)</h2><div class="row"><span id="repState" class="chip" data-s="off"><i></i><span>Not connected</span></span>' +
    '<button id="repAsk">Ask for status</button><button id="repClock">Set its clock from this tablet</button><button id="repLink">Connections...</button></div>' +
    '<div class="repout" id="repOut" aria-live="off">Nothing yet.</div>' +
    '<form id="repForm" class="row"><input id="repIn" type="text" class="grow" placeholder="A repeater command, for example: ver" autocomplete="off"><button class="pri">Send</button></form>' +
    '<p class="small muted">This is the repeater\'s own text, shown as it arrives and kept in today\'s repeater log (Files screen). The station asks it for status every 30 seconds while it is connected.</p>';
  document.querySelector("#tab-station .grid").appendChild(rc);
  $("repAsk").addEventListener("click", () => { repSend("stats-core"); repSend("stats-radio"); repSend("neighbors"); });
  $("repClock").addEventListener("click", () => { repSend("time " + Math.round(Date.now() / 1000)); });
  $("repLink").addEventListener("click", () => openGate("repeater", true));
  $("repForm").addEventListener("submit", (e) => { e.preventDefault(); const v = $("repIn").value.trim(); if (v) { repSend(v); $("repIn").value = ""; } });

  /* ---- events from the tablet ---- */
  window.cairnNative = {
    emit(type, json) { let p; try { p = JSON.parse(json); } catch (e) { return; } onEvent(type, p); },
    resumed() { refreshLinks(); },
    back() {
      if (!$("fViewCard").hidden) { $("fViewCard").hidden = true; return; }
      if (G.open) { const i = STEPS.findIndex((s) => s.id === G.step); if (i > 0) { G.step = STEPS[i - 1].id; G.held = true; renderGate(); } }
    },
  };
  function onEvent(type, p) {
    if (type === "net") { SX.net = p; refreshLinks(); if (p.online && Date.now() - SX.syncAt > 1800000) stSync(true); if (S.tab === "online") stRenderOnline(); }
    else if (type === "ble") { const was = SX.ble.state; SX.ble = p; if (p.state === "connected") { if (S.mode !== "serial") companionUp(); } else if (was === "connected") companionDown(p.msg || "The radio link was lost."); refreshLinks(); }
    else if (type === "bleDev") { if (p.error) SX.scanNote = p.error; else SX.devs.set(p.addr, p); if (G.open && G.step === "radio") renderGate(); }
    else if (type === "bleData") { p.t.split("\n").forEach((l) => { if (l) onLine(l); }); }
    else if (type === "usb") { const was = SX.usb.state; SX.usb = p; if (p.state === "open" && was !== "open") repUp(); if (p.state !== "open") repDown(); refreshLinks(); }
    else if (type === "usbData") { p.t.split("\n").forEach((l) => { if (l) repLine(l); }); }
    else if (type === "http") { const w = SX.waiters.get(p.key); if (w) w(p); }
    else if (type === "dl") { SX.dl[p.name] = p; renderDl(); if (p.state === "done") dlDone(p); }
    else if (type === "perm") { if (G.open && G.step === "radio") { if (p.ble) startScan(); renderGate(); } }
  }

  /* ---- companion radio (Bluetooth) ---- */
  function companionUp() {
    stopDemo();
    S.mode = "serial"; setStatus("wait", "Connected, waiting for data");
    const hello = () => send("nova hello");
    hello(); send("cairn id"); send("cairn chans"); S.dumping = true; send("cairn dump");
    clearInterval(S.keep); S.keep = setInterval(hello, 3000);
    SX.hadRadio = true;
  }
  function companionDown(why) {
    clearInterval(S.keep); S.keep = null;
    if (S.mode === "serial") { S.mode = "off"; S.frame = null; }
    setStatus("off", why);
  }
  window.stSend = function (line) {
    if (SX.ble.state !== "connected") { toast("The radio is not connected. Open Connections."); return; }
    const r = call("bleWrite", line);
    if (r !== "ok") toast(r || "Could not send.");
  };

  /* ---- repeater (USB) ---- */
  function repPush(t) {
    SX.rep.push(t); if (SX.rep.length > 120) SX.rep.shift();
    const e = $("repOut"); if (e) { e.textContent = SX.rep.join("\n"); e.scrollTop = e.scrollHeight; }
  }
  function repSend(c) { const r = call("usbWrite", c); if (r === "ok") repPush("> " + c); else repPush("! " + (r || "not connected")); }
  function repLine(l) { repPush(l); call("log", "repeater", new Date().toISOString() + " " + l); }
  function repUp() {
    SX.rep = []; repPush("Connected. Asking the repeater for its version...");
    setTimeout(() => repSend("ver"), 700); setTimeout(() => { repSend("stats-core"); repSend("stats-radio"); repSend("neighbors"); }, 1800);
    clearInterval(SX.repTimer);
    SX.repTimer = setInterval(() => { if (SX.usb.state === "open") { repSend("stats-core"); repSend("stats-radio"); repSend("neighbors"); } }, 30000);
  }
  function repDown() { clearInterval(SX.repTimer); SX.repTimer = null; }

  /* ---- links in the header ---- */
  function refreshLinks() {
    const set = (id, s, t) => { const e = $(id); e.dataset.s = s; if (t) e.title = t; };
    const b = SX.ble.state;
    set("lnkRadio", b === "connected" ? "live" : b === "connecting" || b === "pairing" ? "wait" : "bad", "Companion radio: " + (SX.ble.msg || b));
    const u = SX.usb.state;
    set("lnkRep", u === "open" ? "live" : u === "asking" ? "wait" : "bad", "Repeater: " + (SX.usb.msg || u));
    set("lnkWifi", SX.net.online ? "live" : SX.net.wifi ? "wait" : "off", SX.net.online ? "Wi-Fi works" : SX.net.wifi ? "Wi-Fi is connected but there is no internet" : "No Wi-Fi: working offline");
    const rs = $("repState"); rs.dataset.s = u === "open" ? "live" : u === "asking" ? "wait" : "off"; rs.lastElementChild.textContent = SX.usb.msg || (u === "open" ? "Connected" : "Not connected");
    const online = SX.net.online;
    ["btnGem", "onGem"].forEach((id) => { const e = $(id); if (e) { e.disabled = !online; e.title = online ? "Open Gemini" : "Gemini needs the internet, and there is none right now."; } });
    const alerts = liveAlerts();
    $("lnkWx").hidden = !alerts.length;
    if (alerts.length) $("lnkWxT").textContent = alerts[0].e + (alerts.length > 1 ? " +" + (alerts.length - 1) : "");
    if (G.open) renderGate();
  }

  /* ---- the start-up steps ---- */
  const stepOk = (id) => (id === "wifi" ? true : id === "repeater" ? SX.usb.state === "open" : SX.ble.state === "connected");
  function openGate(step, held, why) {
    G.open = true; G.step = step; G.held = !!held; G.why = why || ""; clearTimeout(G.timer); G.timer = null;
    if (step === "radio") startScan();
    renderGate();
  }
  function closeGate() { G.open = false; clearTimeout(G.timer); G.timer = null; SX.badSince = 0; renderGate(); showTab(S.tab || "home"); }
  function nextStep() {
    const i = STEPS.findIndex((s) => s.id === G.step);
    if (G.step === "wifi") G.passedWifi = true;
    if (i >= STEPS.length - 1) { closeGate(); return; }
    openGate(STEPS[i + 1].id, G.held);
  }
  function startScan() {
    if (call("bleAllowed") !== true && call("bleAllowed") !== "true") { call("bleAsk"); return; }
    const r = call("bleScan", true); SX.scanNote = r && r !== "ok" ? r : "";
  }
  function esc2(s) { return esc(s); }
  function renderGate() {
    const g = $("gate"); g.hidden = !G.open;
    if (!G.open) return;
    $("gSteps").innerHTML = STEPS.map((s) => '<span class="' + (s.id === G.step ? "cur" : (s.id === "wifi" ? G.passedWifi : stepOk(s.id)) ? "done" : "") + '">' + esc2(s.t) + "<small>" + esc2(s.s) + "</small></span>").join("");
    const body = $("gBody"), btns = $("gBtns"); let ok = stepOk(G.step), foot = "";
    const state = (s, t) => '<div class="gstate" data-s="' + s + '"><i></i><span>' + esc2(t) + "</span></div>";
    let title = "", lead = "", html = "", b = "";
    if (G.why) lead = G.why;
    if (G.step === "wifi") {
      title = "Wi-Fi (optional)";
      lead = (lead ? lead + " " : "") + "Wi-Fi lets CAIRN fetch weather alerts, earthquake reports, offline maps and updates, and turns on Gemini. The station works without it.";
      ok = SX.net.wifi;
      html = state(SX.net.online ? "ok" : SX.net.wifi ? "wait" : "bad", SX.net.online ? "Connected, and the internet works." : SX.net.wifi ? "Connected to Wi-Fi, but the internet is not answering." : "Not connected to Wi-Fi.");
      b = '<button data-act="wifi">Open Wi-Fi settings</button><span class="grow"></span>' + (SX.net.wifi ? '<button class="pri" data-act="next">Continue</button>' : '<button class="pri" data-act="next">Skip - work offline</button>');
    } else if (G.step === "repeater") {
      title = "Repeater (USB cable)"; lead = (lead ? lead + " " : "") + "Plug the repeater's USB cable into the tablet (or its hub). The station needs the repeater to see what is on the air nearby.";
      const list = jcall("usbList") || [];
      html = state(SX.usb.state === "open" ? "ok" : SX.usb.state === "asking" ? "wait" : "bad", SX.usb.state === "open" ? SX.usb.msg || "Connected." : SX.usb.msg || (list.length ? "Choose the repeater below." : "No USB device found yet."));
      html += '<div class="devs">' + list.map((d) => '<div class="dev"><div class="nm">' + esc2(d.name) + "<small>" + (d.chosen ? "Used last time. " : "") + (d.serial ? "Serial " + esc2(d.serial) : "") + '</small></div><button class="pri" data-act="usb" data-id="' + esc2(d.id) + '">' + (SX.usb.state === "open" && d.chosen ? "Reconnect" : "Connect") + "</button></div>").join("") + "</div>";
      if (list.length > 1) html += '<p class="small muted">More than one USB device is plugged in. If the companion radio is on the hub for power, it shows up here too. Pick the repeater (unplug the radio\'s cable and press Refresh if you are not sure which is which).</p>';
      b = '<button data-act="back">Back</button><button data-act="usbref">Refresh</button><span class="grow"></span><button class="pri" data-act="next"' + (ok ? "" : " disabled") + ">Continue</button>";
    } else {
      title = "Companion radio (Bluetooth)"; lead = (lead ? lead + " " : "") + "This is the radio that sends and receives your messages and feeds the dashboard. The station cannot start without it.";
      const devs = Array.from(SX.devs.values()).sort((a, c) => (c.bonded - a.bonded) || (c.rssi - a.rssi));
      const last = (SX.info.lastRadio || {}).addr;
      html = state(SX.ble.state === "connected" ? "ok" : (SX.ble.state === "connecting" || SX.ble.state === "pairing") ? "wait" : "bad", SX.ble.msg || (SX.ble.state === "connected" ? "Connected." : "Not connected. Find the radio below."));
      if (call("bleAllowed") !== true && call("bleAllowed") !== "true") html += '<p class="small">Android needs your permission to use Bluetooth. Tap Find radios and then Allow.</p>';
      html += '<div class="devs">' + devs.map((d) => '<div class="dev"><div class="nm">' + esc2(d.name) + "<small>" + (d.bonded ? "Paired before. " : "New. ") + (d.rssi ? "Signal " + d.rssi + " dBm" : "") + '</small></div><button class="pri" data-act="ble" data-addr="' + esc2(d.addr) + '" data-name="' + esc2(d.name) + '">' + (d.addr === last ? "Reconnect" : "Connect") + "</button></div>").join("") + "</div>";
      if (!devs.length) html += '<p class="small muted">' + esc2(SX.scanNote || "Searching... Make sure the radio is on and close to the tablet. Only one device can use the radio\'s Bluetooth at a time, so close the MeshCore app on any phone first.") + "</p>";
      html += '<p class="small muted">The first time, Android asks for a PIN: type the PIN shown on the radio\'s screen.</p>';
      b = '<button data-act="back">Back</button><button data-act="scan">Find radios</button><button data-act="btset">Bluetooth settings</button><span class="grow"></span><button class="pri" data-act="next"' + (ok ? "" : " disabled") + ">Open the dashboard</button>";
    }
    $("gTitle").textContent = title; $("gLead").textContent = lead; body.innerHTML = html; btns.innerHTML = b;
    // move on by itself when this step is already done, unless the person stopped it
    const auto = !G.held && ok && !(G.step === "wifi" && !SX.net.wifi);
    if (auto && !G.timer) G.timer = setTimeout(() => { G.timer = null; if (!G.held && G.open) nextStep(); }, 2200);
    if (!auto) { clearTimeout(G.timer); G.timer = null; }
    foot = auto ? "Continuing in a moment... tap anything on this screen to stay here." : "";
    $("gFoot").textContent = foot;
  }
  $("gate").addEventListener("click", (e) => {
    G.held = true; clearTimeout(G.timer); G.timer = null; $("gFoot").textContent = "";
    const t = e.target.closest("[data-act]"); if (!t) return;
    const a = t.dataset.act;
    if (a === "wifi") call("wifiSettings");
    else if (a === "next") nextStep();
    else if (a === "back") { const i = STEPS.findIndex((s) => s.id === G.step); if (i > 0) openGate(STEPS[i - 1].id, true); }
    else if (a === "usb") { const r = call("usbOpen", t.dataset.id); if (r && r !== "ok") SX.usb = { state: "error", msg: r }; renderGate(); }
    else if (a === "usbref") renderGate();
    else if (a === "ble") { const r = call("bleOpen", t.dataset.addr, t.dataset.name); if (r === "needperm") call("bleAsk"); else if (r && r !== "ok") { SX.ble = { state: "error", msg: r }; } renderGate(); }
    else if (a === "scan") { SX.devs.clear(); startScan(); renderGate(); }
    else if (a === "btset") call("bluetoothSettings");
  });
  // a tick keeps the USB list and the "lost link" watch fresh
  setInterval(() => {
    if (G.open && G.step === "repeater") renderGate();
    if (G.open || S.mode === "demo") { SX.badSince = 0; return; }
    const bad = !stepOk("repeater") ? "repeater" : !stepOk("radio") ? "radio" : "";
    if (!bad) { SX.badSince = 0; return; }
    if (!SX.badSince) SX.badSince = Date.now();
    if (Date.now() - SX.badSince > 10000) openGate(bad, true, bad === "radio" ? "The link to the companion radio was lost." : "The repeater cable is not connected.");
  }, 2000);

  /* ---- saving, logging ---- */
  window.stLog = (kind, line) => { call("log", kind, line); };
  window.stSave = (name, text) => {
    const n = name.replace(/[^A-Za-z0-9._ -]/g, "-");
    const r = call("fileWrite", "reports", n, text);
    toast(r ? "Saved in Files: " + n : "Could not save " + n + " on the tablet.");
    if (S.tab === "files") stRenderFiles();
  };

  /* ---- Files screen ---- */
  window.stRenderFiles = function () {
    const k = $("fKind").value, all = jcall("fileList") || [], list = k ? all.filter((f) => f.kind === k) : all;
    $("fNote").textContent = all.length ? all.length + " file" + (all.length === 1 ? "" : "s") + " on this tablet." : "Nothing saved yet. The radio's journal and the repeater's log appear here as they are recorded.";
    $("fBody").innerHTML = list.map((f) => {
      const text = /\.(log|txt|csv|json|jsonl)$/i.test(f.name), apk = /\.apk$/i.test(f.name);
      return "<tr><td>" + esc(f.name) + '<br><small class="muted">' + esc(f.kind) + "</small></td><td>" + fmtB(f.size) + "</td><td>" + esc(when(f.mtime)) + '</td><td class="act">' +
        (text ? '<button data-fa="view" data-p="' + esc(f.path) + '">View</button>' : "") + (apk ? '<button class="pri" data-fa="install" data-p="' + esc(f.path) + '">Install</button>' : "") +
        '<button data-fa="share" data-p="' + esc(f.path) + '">Send</button><button data-fa="export" data-p="' + esc(f.path) + '" title="Copy to the tablet\'s Download/CAIRN folder">Save to Downloads</button><button class="danger" data-fa="del" data-p="' + esc(f.path) + '">Delete</button></td></tr>';
    }).join("");
  };
  $("fKind").addEventListener("change", () => stRenderFiles());
  $("fRefresh").addEventListener("click", () => stRenderFiles());
  $("fCsv").addEventListener("click", () => { downloadCsv(); });
  $("fViewClose").addEventListener("click", () => { $("fViewCard").hidden = true; });
  $("fBody").addEventListener("click", (e) => {
    const t = e.target.closest("[data-fa]"); if (!t) return; const p = t.dataset.p, a = t.dataset.fa;
    if (a === "view") { $("fViewTitle").textContent = p.split("/")[1]; $("fView").textContent = call("fileRead", p) || "(empty)"; $("fViewCard").hidden = false; $("fViewCard").scrollIntoView(); }
    else if (a === "share") { if (call("fileShare", p) !== true) toast("Could not open the share sheet."); }
    else if (a === "export") { const r = call("fileExport", p); toast(r ? "Copied to " + r : "Could not copy it to Downloads."); }
    else if (a === "install") { const r = call("installApk", p); toast(r === "ok" ? "Android will ask you to confirm the update." : r); }
    else if (a === "del") { if (confirm("Delete " + p.split("/")[1] + " from this tablet? This cannot be undone.")) { call("fileDelete", p); stRenderFiles(); } }
  });

  /* ---- Online screen ---- */
  const waitFor = (key, ms) => new Promise((res) => { const t = setTimeout(() => { SX.waiters.delete(key); res({ ok: false, msg: "No answer (timed out)." }); }, ms || 40000); SX.waiters.set(key, (p) => { clearTimeout(t); SX.waiters.delete(key); res(p); }); });
  async function getJson(key, url) {
    const w = waitFor(key), r = call("httpGet", key, url);
    if (r !== "ok") { SX.waiters.delete(key); return { ok: false, msg: r || "Could not start." }; }
    const p = await w; if (!p.ok) return { ok: false, msg: p.msg || "failed", status: p.status };
    try { const c = JSON.parse(call("cached", key) || "null"); return { ok: true, data: JSON.parse(c.body), at: c.at }; } catch (e) { return { ok: false, msg: "The answer could not be read." }; }
  }
  function wxPos() {
    const m = ($("wxPos").value || "").match(/(-?\d+(?:\.\d+)?)\s*[, ]\s*(-?\d+(?:\.\d+)?)/);
    if (m) return [+m[1], +m[2]];
    const h = here(); if (h) { store.set("cairn-lastpos", h.join(",")); return h; }
    const lp = (store.get("cairn-lastpos") || "").split(",").map(Number);
    return lp.length === 2 && isFinite(lp[0]) && isFinite(lp[1]) && (lp[0] || lp[1]) ? lp : null;
  }
  const SEV = { Extreme: 0, Severe: 1, Moderate: 2, Minor: 3, Unknown: 4 };
  function liveAlerts() {
    const w = jget("cairn-wx"); if (!w || !w.alerts) return [];
    return w.alerts.filter((a) => !a.x || Date.parse(a.x) > Date.now()).sort((a, b) => (SEV[a.sev] ?? 4) - (SEV[b.sev] ?? 4));
  }
  async function stWeather(quiet) {
    const pos = wxPos();
    if (!pos) { $("wxNote").textContent = "No position yet. Type latitude, longitude above, or wait for the radio's GPS fix."; return false; }
    if (!SX.net.online) { if (!quiet) $("wxNote").textContent = "No internet right now. Showing the last answer."; return false; }
    const la = +pos[0].toFixed(4), lo = +pos[1].toFixed(4);
    $("wxNote").textContent = "Asking the National Weather Service...";
    const pt = await getJson("wxpoint", "https://api.weather.gov/points/" + la + "," + lo);
    if (!pt.ok) { $("wxNote").textContent = pt.status === 404 ? "The National Weather Service has no forecast for this place (it covers the United States and its territories)." : "Weather: " + pt.msg; return false; }
    const pr = pt.data.properties || {}, loc = (pr.relativeLocation || {}).properties || {};
    const [fc, al] = await Promise.all([pr.forecast ? getJson("wxfc", pr.forecast) : Promise.resolve({ ok: false }), getJson("wxalerts", "https://api.weather.gov/alerts/active?point=" + la + "," + lo)]);
    const old = jget("cairn-wx"), same = old && Math.abs(old.pos[0] - la) < 0.2 && Math.abs(old.pos[1] - lo) < 0.2;
    const vm = { pos: [la, lo], at: Date.now(), where: loc.city ? loc.city + ", " + loc.state : "", fc: same ? old.fc : null, alerts: same ? old.alerts : null, fcAt: same ? old.fcAt : 0 };
    if (fc.ok) { vm.fc = ((fc.data.properties || {}).periods || []).slice(0, 4).map((p) => ({ n: p.name, t: p.temperature, u: p.temperatureUnit, s: p.shortForecast, w: ((p.windSpeed || "") + " " + (p.windDirection || "")).trim() })); vm.fcAt = Date.now(); }
    if (al.ok) vm.alerts = (al.data.features || []).map((f) => { const q = f.properties || {}; return { e: q.event || "Alert", h: q.headline || "", sev: q.severity || "Unknown", a: q.areaDesc || "", x: q.expires || "", i: q.instruction || "" }; });
    store.set("cairn-wx", JSON.stringify(vm));
    $("wxNote").textContent = fc.ok || al.ok ? "" : "Weather: " + (fc.msg || al.msg || "failed");
    refreshLinks(); if (S.tab === "online") renderWx();
    return fc.ok || al.ok;
  }
  function renderWx() {
    const w = jget("cairn-wx");
    if (!w) { $("wxWhere").textContent = "Nothing fetched yet."; $("wxAlerts").textContent = ""; $("wxFc").textContent = ""; return; }
    const age = Date.now() - w.at;
    $("wxWhere").innerHTML = esc((w.where || "Near " + w.pos.join(", ")) + ". Updated " + ago(w.at) + ".") + (age > 10800000 ? ' <span class="stale">May be out of date.</span>' : "");
    const al = liveAlerts();
    $("wxAlerts").innerHTML = w.alerts == null ? "" : al.length ? al.map((a) => '<div class="wxalert' + (a.sev === "Minor" || a.sev === "Moderate" ? " minor" : "") + '"><b>' + esc(a.e) + " (" + esc(a.sev) + ")</b>" + esc(a.h) + '<br><span class="small muted">' + esc(a.a) + (a.x ? " - until " + esc(when(Date.parse(a.x))) : "") + "</span>" + (a.i ? '<br><span class="small">' + esc(a.i) + "</span>" : "") + "</div>").join("") : '<p class="small"><b>No official weather alerts</b> for this place right now.</p>';
    $("wxFc").innerHTML = (w.fc || []).map((p) => '<div class="fc"><b>' + esc(p.n) + '</b><span class="t">' + esc(p.t) + "&deg;" + esc(p.u) + "</span>" + esc(p.s) + '<br><span class="small muted">Wind ' + esc(p.w) + "</span></div>").join("");
  }
  async function stQuakes(quiet) {
    const pos = wxPos();
    if (!pos) { $("eqNote").textContent = "No position yet."; return false; }
    if (!SX.net.online) { if (!quiet) $("eqNote").textContent = "No internet right now. Showing the last answer."; return false; }
    const since = new Date(Date.now() - 7 * 86400000).toISOString().slice(0, 10);
    const r = await getJson("quakes", "https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&latitude=" + pos[0].toFixed(3) + "&longitude=" + pos[1].toFixed(3) + "&maxradiuskm=500&minmagnitude=2&limit=15&orderby=time&starttime=" + since);
    if (!r.ok) { $("eqNote").textContent = "Earthquakes: " + r.msg; return false; }
    const list = (r.data.features || []).map((f) => ({ m: (f.properties || {}).mag, p: (f.properties || {}).place || "", t: (f.properties || {}).time || 0 }));
    store.set("cairn-eq", JSON.stringify({ at: Date.now(), pos, list })); $("eqNote").textContent = "";
    if (S.tab === "online") renderEq();
    return true;
  }
  function renderEq() {
    const q = jget("cairn-eq");
    if (!q) { $("eqList").textContent = "Nothing fetched yet."; return; }
    $("eqList").innerHTML = (q.list.length ? "<ul style='padding-left:18px;margin:6px 0'>" + q.list.slice(0, 8).map((e) => "<li><b>M" + esc(e.m) + "</b> " + esc(e.p) + ' <span class="small muted">' + esc(ago(e.t)) + "</span></li>").join("") + "</ul>" : "<p>No earthquakes of magnitude 2 or more within 500 km in the last 7 days.</p>") +
      '<p class="small muted">Updated ' + esc(ago(q.at)) + "." + (Date.now() - q.at > 10800000 ? ' <span class="stale">May be out of date.</span>' : "") + "</p>";
  }
  const verNum = (v) => { const m = /(\d+)\.(\d+)\.(\d+)/.exec(v || ""); return m ? (+m[1]) * 1e6 + (+m[2]) * 1e3 + (+m[3]) : 0; };
  async function stUpdates(quiet) {
    if (!SX.net.online) { if (!quiet) $("upNote").textContent = "No internet right now. Showing the last answer."; return false; }
    const r = await getJson("releases", "https://api.github.com/repos/propertymonitor-web/lodestar-cairn/releases?per_page=10");
    if (!r.ok) { $("upNote").textContent = "Updates: " + r.msg; return false; }
    const rel = (r.data || []).find((x) => !x.draft && /^cairn-/i.test(x.tag_name || ""));
    if (!rel) { $("upNote").textContent = "No CAIRN release found."; return false; }
    store.set("cairn-up", JSON.stringify({ at: Date.now(), tag: rel.tag_name, name: rel.name || "", date: rel.published_at, assets: (rel.assets || []).map((a) => ({ n: a.name, s: a.size, u: a.browser_download_url })) }));
    $("upNote").textContent = ""; if (S.tab === "online") renderUp();
    return true;
  }
  function renderUp() {
    $("upVer").textContent = (SX.info.app || DASH_VER);
    const u = jget("cairn-up");
    if (!u) { $("upList").textContent = "Not checked yet."; return; }
    const apk = u.assets.find((a) => /^cairn-station-.*\.apk$/i.test(a.n)), av = apk ? verNum(apk.n) : 0, mine = verNum(SX.info.app || DASH_VER);
    $("upList").innerHTML = '<p>Latest release: <b>' + esc(u.tag) + "</b>" + (u.date ? ' <span class="small muted">(' + esc(when(Date.parse(u.date))) + ")</span>" : "") + "</p>" +
      (apk ? (av > mine ? '<p class="wxalert minor"><b>A newer tablet app is available.</b>Download it, then open Files and tap Install.</p>' : "<p>This tablet app is up to date.</p>") : "") +
      u.assets.filter((a) => /\.(apk|bin|pdf|html)$/i.test(a.n)).map((a) => '<div class="row" style="margin:4px 0"><span class="grow">' + esc(a.n) + ' <span class="small muted">' + fmtB(a.s) + '</span></span><button data-dl="release" data-n="' + esc(a.n) + '" data-u="' + esc(a.u) + '">Download</button></div>').join("") +
      '<p class="small muted">Checked ' + esc(ago(u.at)) + ". Radio firmware is flashed from a computer (see the setup guide); the .bin files are here so you have them with no internet.</p>";
  }
  $("upList").addEventListener("click", (e) => { const t = e.target.closest("[data-dl]"); if (t) startDl(t.dataset.dl, t.dataset.n, t.dataset.u); });
  function startDl(kind, name, url) {
    if (!SX.net.online) { toast("Downloading needs Wi-Fi with a working internet connection."); return; }
    const r = call("download", kind, name, url);
    if (r === "ok") toast("Downloading " + name + "..."); else toast(r || "Could not start the download.");
  }
  function renderDl() {
    const items = Object.values(SX.dl).filter((d) => d.state === "run" || d.state === "error");
    const html = items.map((d) => { const pct = d.total > 0 ? Math.round(100 * d.got / d.total) : 0; return "<div><b>" + esc(d.name) + "</b> " + (d.state === "run" ? (d.total > 0 ? pct + "% of " + fmtB(d.total) : fmtB(d.got)) + ' <button data-cancel="' + esc(d.name) + '">Stop</button><div class="bar2"><i style="width:' + pct + '%"></i></div>' : '<span class="stale">' + esc(d.msg || "failed") + "</span>") + "</div>"; }).join("");
    ["dlList", "dlStatus"].forEach((id) => { const e = $(id); if (e) e.innerHTML = html; });
  }
  document.addEventListener("click", (e) => { const t = e.target.closest("[data-cancel]"); if (t) call("downloadCancel", t.dataset.cancel); });
  function dlDone(p) {
    if (p.kind === "map") { toast("Map downloaded: " + p.name); stAddMap(p.name); }
    else toast("Saved in Files, Downloads: " + p.name);
    setTimeout(() => { delete SX.dl[p.name]; renderDl(); }, 3000);
  }

  /* ---- offline maps stored on the tablet ---- */
  async function stAddMap(fileName, size) {
    const base = fileName.replace(/\.pmtiles$/i, ""), src = { url: location.origin + "/maps/" + encodeURIComponent(fileName) };
    try {
      const info = await readHeader(src);
      const rec = { name: base, title: base.replace(/[-_]+/g, " ").replace(/\b\w/g, (c) => c.toUpperCase()), url: src.url, native: true, size: size || 0, added: Date.now(),
        bounds: [info.h.minLat, info.h.minLon, info.h.maxLat, info.h.maxLon], minZoom: info.h.minZoom, maxZoom: info.h.maxZoom };
      S.off.set(base, rec); renderOffList(); fillTileSel();
    } catch (e) { offNoteSet(fileName + ": " + e.message + "."); }
  }
  window.stLoadMaps = async function () { const l = jcall("mapList") || []; for (const m of l) await stAddMap(m.name, m.size); };

  /* ---- Map screen: the region list downloads straight to the tablet ---- */
  const dlStatus = document.createElement("div"); dlStatus.id = "dlStatus"; $("regNote").insertAdjacentElement("afterend", dlStatus);
  $("regSel").addEventListener("change", () => { $("regNote").textContent = $("regSel").value ? "This downloads to the tablet (it needs Wi-Fi) and then appears in the list below." : ""; });
  $("regDl").addEventListener("click", (e) => {
    e.preventDefault(); e.stopPropagation();
    const u = $("regSel").value; if (!u) return;
    startDl("map", decodeURIComponent(u.split("?")[0].split("/").pop()), u);
  }, true);

  /* ---- Gemini ---- */
  function gemini() {
    const r = call("gemini");
    if (r === "offline") toast("Gemini needs the internet, and there is none right now.");
    else if (r === "web") toast("The Gemini app is not installed, so its web page is opening.");
    else if (r !== "ok") toast("Could not open Gemini: " + r);
  }
  $("btnGem").addEventListener("click", gemini);
  $("onGem").addEventListener("click", gemini);
  $("onGemNote").textContent = "Gemini is Google's assistant on this tablet. It needs the internet to answer, so the button is off when there is none. It opens as its own app: press Home (or swipe up) to come back to CAIRN.";
  $("onWifi").addEventListener("click", () => call("wifiSettings"));

  /* ---- Online screen: fetch everything ---- */
  window.stRenderOnline = function () {
    const n = SX.net;
    $("onState").innerHTML = '<span class="chip" data-s="' + (n.online ? "live" : n.wifi ? "wait" : "off") + '"><i></i>' + (n.online ? "Wi-Fi connected, internet works" : n.wifi ? "Wi-Fi connected, but no internet" : "No Wi-Fi: working offline") + "</span>";
    $("onSync").disabled = !n.online; $("wxGo").disabled = $("eqGo").disabled = $("upGo").disabled = !n.online;
    refreshLinks(); renderWx(); renderEq(); renderUp(); renderDl();
  };
  async function stSync(quiet) {
    if (!SX.net.online) { if (!quiet) toast("No internet right now."); return; }
    SX.syncAt = Date.now();
    if (!quiet) $("onSyncNote").textContent = "Fetching weather, earthquakes, updates and the map list...";
    const res = { weather: await stWeather(true), earthquakes: await stQuakes(true), updates: await stUpdates(true) };
    try { await loadRegionList(); res.maps = true; } catch (e) { res.maps = false; }
    const bits = Object.keys(res).map((k) => k + (res[k] ? " ok" : " not available"));
    $("onSyncNote").textContent = "Fetched " + new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }) + ": " + bits.join(", ") + ".";
    if (S.tab === "online") stRenderOnline();
  }
  $("onSync").addEventListener("click", () => stSync(false));
  $("wxGo").addEventListener("click", () => stWeather(false));
  $("eqGo").addEventListener("click", () => stQuakes(false));
  $("upGo").addEventListener("click", () => stUpdates(false));
  $("wxPos").value = ""; 

  window.stOpenGate = () => openGate(stepOk("repeater") ? (stepOk("radio") ? "wifi" : "radio") : "repeater", true);

  /* ---- start ---- */
  SX.info = jcall("info") || {};
  const snap = jcall("snapshot") || {};
  if (snap.net) SX.net = snap.net;
  if (snap.ble) SX.ble = snap.ble;
  if (snap.usb) SX.usb = snap.usb;
  const nn = jcall("netNow"); if (nn) SX.net = nn;
  const lr = SX.info.lastRadio;
  if (lr && lr.addr && SX.ble.state !== "connected" && (call("bleAllowed") === true || call("bleAllowed") === "true")) call("bleOpen", lr.addr, lr.name || "MeshCore radio");
  if (SX.ble.state === "connected" && S.mode !== "serial") companionUp();
  if (SX.usb.state === "open") repUp();
  document.querySelector("#banner .s").textContent = "Open Connections (top right) to reconnect the radio and the repeater.";
  refreshLinks();
  openGate("wifi", false);
})();
