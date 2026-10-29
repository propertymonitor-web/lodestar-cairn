// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

import android.app.Activity
import android.content.ComponentName
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.webkit.JavascriptInterface
import org.json.JSONArray
import org.json.JSONObject

/**
 * What the dashboard page can ask the tablet to do. The page is part of this app (it is loaded from the app's own
 * files), and nothing else can reach this object. Every call returns quickly; slow work reports back through
 * events (see Hub.emit and the dashboard's window.cairnNative).
 */
class Bridge(private val act: Activity, private val hub: Hub) {
    private fun s(o: Any) = o.toString()

    @JavascriptInterface fun info(): String = JSONObject()
        .put("app", BuildInfo.VERSION).put("android", android.os.Build.VERSION.RELEASE).put("sdk", android.os.Build.VERSION.SDK_INT)
        .put("model", android.os.Build.MODEL ?: "").put("gemini", geminiInstalled())
        .put("lastRadio", JSONObject().put("addr", hub.ble.lastAddr()).put("name", hub.ble.lastName())).toString()

    /** Where everything stands right now, for a page that has just started. */
    @JavascriptInterface fun snapshot(): String = hub.snapshot().toString()

    // ---- Wi-Fi ----------------------------------------------------------------------------------
    @JavascriptInterface fun wifiSettings() { act.runOnUiThread { try { act.startActivity(Intent(Settings.ACTION_WIFI_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) { } } }
    @JavascriptInterface fun androidSettings() { act.runOnUiThread { try { act.startActivity(Intent(Settings.ACTION_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) { } } }
    @JavascriptInterface fun bluetoothSettings() { act.runOnUiThread { try { act.startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) { } } }
    @JavascriptInterface fun netNow(): String { hub.net.refresh(); return hub.net.state().toString() }

    // ---- the companion radio (Bluetooth) ----------------------------------------------------------
    @JavascriptInterface fun bleAllowed(): Boolean = hub.ble.hasPermission()
    @JavascriptInterface fun bleAsk() { (act as MainActivity).askBluetoothPermission() }
    @JavascriptInterface fun bleScan(on: Boolean): String = hub.ble.scan(on)
    @JavascriptInterface fun bleOpen(addr: String, name: String): String = hub.ble.open(addr, name)
    @JavascriptInterface fun bleClose() { hub.ble.close() }
    @JavascriptInterface fun bleWrite(line: String): String = hub.ble.write(line)

    // ---- the repeater (USB cable) -----------------------------------------------------------------
    @JavascriptInterface fun usbList(): String = try { hub.usb.list().toString() } catch (e: Exception) { "[]" }
    @JavascriptInterface fun usbOpen(id: String): String = hub.usb.open(id)
    @JavascriptInterface fun usbClose() { hub.usb.close() }
    @JavascriptInterface fun usbWrite(text: String): String = hub.usb.write(text)

    // ---- files, logs --------------------------------------------------------------------------------
    @JavascriptInterface fun log(kind: String, line: String) { hub.store.log(kind, line) }
    @JavascriptInterface fun fileList(): String = hub.store.list().toString()
    @JavascriptInterface fun fileRead(path: String): String = hub.store.read(path) ?: ""
    @JavascriptInterface fun fileWrite(kind: String, name: String, text: String): String = hub.store.write(kind, name, text) ?: ""
    @JavascriptInterface fun fileDelete(path: String): Boolean = hub.store.delete(path)
    @JavascriptInterface fun fileShare(path: String): Boolean = hub.store.share(path)
    @JavascriptInterface fun fileExport(path: String): String = hub.store.exportToDownloads(path) ?: ""
    @JavascriptInterface fun installApk(path: String): String = hub.store.installApk(path)

    // ---- online: small lookups, downloads, maps -----------------------------------------------------
    @JavascriptInterface fun httpGet(key: String, url: String): String { if (!hub.http.allowed(url)) return "That address is not allowed."; hub.http.get(key, url); return "ok" }
    @JavascriptInterface fun cached(key: String): String = hub.store.cacheGet(key)?.toString() ?: ""
    @JavascriptInterface fun download(kind: String, name: String, url: String): String = hub.http.download(kind, name, url)
    @JavascriptInterface fun downloadCancel(name: String) { hub.http.cancel(name) }
    @JavascriptInterface fun mapList(): String = hub.store.mapList().toString()
    @JavascriptInterface fun mapDelete(name: String): Boolean = hub.store.mapDelete(name)

    // ---- Gemini ---------------------------------------------------------------------------------------
    private val geminiPkg = "com.google.android.apps.bard"
    private fun geminiInstalled(): Boolean = try { act.packageManager.getPackageInfo(geminiPkg, 0); true } catch (_: Exception) { false }

    /** Gemini needs the internet. The page disables its button with no internet; this checks again. */
    @JavascriptInterface fun gemini(): String {
        hub.net.refresh()
        if (!hub.net.online) return "offline"
        val pm = act.packageManager
        val launch = pm.getLaunchIntentForPackage(geminiPkg)
        return try {
            if (launch != null) { act.runOnUiThread { act.startActivity(launch) }; "ok" }
            else { act.runOnUiThread { act.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://gemini.google.com")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }; "web" }
        } catch (e: Exception) { "error: " + (e.message ?: "") }
    }

    @JavascriptInterface fun keepScreenOn(on: Boolean) { (act as MainActivity).keepOn(on) }
}
