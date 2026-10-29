// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.webkit.WebView
import org.json.JSONObject

/**
 * Holds the pieces (Bluetooth to the radio, USB to the repeater, network watcher, files) and passes their events to
 * the dashboard page. Events that happen while no page is showing are not lost: the page asks for the current state
 * (snapshot) when it starts.
 */
class Hub(val ctx: Context) {
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var web: WebView? = null
    private val last = LinkedHashMap<String, String>()   // latest "state" event of each kind, for a page that starts late

    val store = Store(ctx)
    val net = NetWatch(ctx, this)
    val ble = BleLink(ctx, this)
    val usb = UsbLink(ctx, this)
    val http = Fetcher(this, store)

    fun start() {
        net.start()
        usb.start()
    }

    fun attach(w: WebView?) { web = w }

    /** state events are remembered; data events (lines of text) are not. */
    fun emit(type: String, payload: JSONObject, remember: Boolean = false) {
        val json = payload.toString()
        if (remember) synchronized(last) { last[type] = json }
        val w = web ?: return
        val js = "window.cairnNative&&window.cairnNative.emit(" + JSONObject.quote(type) + "," + JSONObject.quote(json) + ")"
        main.post { try { w.evaluateJavascript(js, null) } catch (_: Exception) { } }
    }

    fun snapshot(): JSONObject {
        val o = JSONObject()
        synchronized(last) { for ((k, v) in last) o.put(k, JSONObject(v)) }
        return o
    }
}
