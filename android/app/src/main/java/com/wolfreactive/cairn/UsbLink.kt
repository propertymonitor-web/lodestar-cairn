// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.util.SerialInputOutputManager
import org.json.JSONArray
import org.json.JSONObject

/**
 * USB cable to the repeater (115200 baud serial; the repeater's own text commands). Its text is shown on the Station
 * screen and kept in the day's repeater log. The link re-opens by itself when the cable is plugged back in.
 */
class UsbLink(private val ctx: Context, private val hub: Hub) {
    private val ACTION_PERM = ctx.packageName + ".USB_PERMISSION"
    private val um = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
    private val main = Handler(Looper.getMainLooper())
    private val prefs: SharedPreferences = ctx.getSharedPreferences("cairn-usb", Context.MODE_PRIVATE)
    private val splitter = LineSplitter()

    @Volatile var state = "idle"; private set
    private var port: UsbSerialPort? = null
    private var io: SerialInputOutputManager? = null
    private var wantedKey = ""             // which cable the person chose: "vid:pid:serial-or-name"
    private var shownName = ""

    private val rcv = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            when (i.action) {
                ACTION_PERM -> {
                    val granted = i.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    val dev = deviceOf(i)
                    if (dev != null && keyOf(dev) == pendingKey) { if (granted) openDevice(dev) else set("denied", "Permission was refused. Tap Connect and allow it.") }
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    val dev = deviceOf(i)
                    if (dev != null && state == "open" && dev.deviceName == openName) { closePort(); set("closed", "The cable was unplugged.") }
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> main.postDelayed({ autoOpen() }, 800)
            }
        }
    }
    private var pendingKey = ""
    private var openName = ""

    @Suppress("DEPRECATION")
    private fun deviceOf(i: Intent): UsbDevice? =
        if (Build.VERSION.SDK_INT >= 33) i.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        else i.getParcelableExtra(UsbManager.EXTRA_DEVICE)

    fun start() {
        val f = IntentFilter().apply { addAction(ACTION_PERM); addAction(UsbManager.ACTION_USB_DEVICE_DETACHED); addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED) }
        if (Build.VERSION.SDK_INT >= 33) ctx.registerReceiver(rcv, f, Context.RECEIVER_NOT_EXPORTED) else ctx.registerReceiver(rcv, f)
        wantedKey = prefs.getString("key", "") ?: ""
        shownName = prefs.getString("name", "") ?: ""
        main.postDelayed({ autoOpen() }, 1500)
    }

    private fun set(s: String, msg: String = "") {
        state = s
        hub.emit("usb", JSONObject().put("state", s).put("msg", msg).put("name", shownName), remember = true)
    }

    private fun keyOf(d: UsbDevice): String {
        val id = try { if (um.hasPermission(d)) d.serialNumber else null } catch (_: Exception) { null }
        return String.format("%04x:%04x:%s", d.vendorId, d.productId, id ?: d.deviceName)
    }

    private fun label(d: UsbDevice): String {
        val p = try { d.productName } catch (_: Exception) { null }
        return (if (p.isNullOrBlank()) "USB serial device" else p) + String.format(" (%04x:%04x)", d.vendorId, d.productId)
    }

    fun list(): JSONArray {
        val a = JSONArray()
        for (drv in UsbSerialProber.getDefaultProber().findAllDrivers(um)) {
            val d = drv.device
            val sn = try { if (um.hasPermission(d)) d.serialNumber else null } catch (_: Exception) { null }
            a.put(JSONObject().put("id", d.deviceName).put("name", label(d)).put("serial", sn ?: "").put("perm", um.hasPermission(d))
                .put("chosen", wantedKey.isNotEmpty() && keyOf(d) == wantedKey))
        }
        return a
    }

    fun open(deviceName: String): String {
        val drv = UsbSerialProber.getDefaultProber().findAllDrivers(um).firstOrNull { it.device.deviceName == deviceName } ?: return "That cable is no longer there."
        val d = drv.device
        shownName = label(d)
        if (um.hasPermission(d)) { remember(d); openDevice(d) }
        else {
            pendingKey = keyOf(d)
            set("asking", "Android is asking permission to use the cable. Tap Allow.")
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0)
            val pi = PendingIntent.getBroadcast(ctx, 0, Intent(ACTION_PERM).setPackage(ctx.packageName), flags)
            um.requestPermission(d, pi)
        }
        return "ok"
    }

    private fun remember(d: UsbDevice) {
        wantedKey = keyOf(d)
        prefs.edit().putString("key", wantedKey).putString("name", shownName).apply()
    }

    /** Re-opens the cable the person chose last time, if it is plugged in and Android has already allowed it. */
    fun autoOpen() {
        if (state == "open" || wantedKey.isEmpty()) return
        for (drv in UsbSerialProber.getDefaultProber().findAllDrivers(um)) {
            val d = drv.device
            if (um.hasPermission(d) && keyOf(d) == wantedKey) { shownName = label(d); openDevice(d); return }
        }
    }

    private fun openDevice(d: UsbDevice) {
        closePort()
        val drv = UsbSerialProber.getDefaultProber().probeDevice(d) ?: run { set("error", "Android found the cable but no serial port on it."); return }
        val conn = um.openDevice(d) ?: run { set("error", "Could not open the cable. Unplug it, plug it in and try again."); return }
        try {
            val p = drv.ports[0]
            p.open(conn)
            p.setParameters(115200, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
            try { p.dtr = true; p.rts = false } catch (_: Exception) { }      // the repeater only talks when the host says it is listening
            port = p; openName = d.deviceName
            splitter.clear()
            val m = SerialInputOutputManager(p, object : SerialInputOutputManager.Listener {
                override fun onNewData(data: ByteArray) {
                    val lines = splitter.feed(data.map { if (it == '\r'.code.toByte()) '\n'.code.toByte() else it }.toByteArray())
                    if (lines.isNotEmpty()) hub.emit("usbData", JSONObject().put("t", lines.joinToString("\n")))
                }
                override fun onRunError(e: Exception) { main.post { closePort(); set("closed", "The cable stopped answering: " + (e.message ?: "")) } }
            })
            io = m; m.start()
            remember(d)
            set("open", "Connected to $shownName")
        } catch (e: Exception) {
            closePort(); set("error", "Could not use the cable: " + (e.message ?: e.javaClass.simpleName))
        }
    }

    fun write(text: String): String {
        val p = port ?: return "The repeater cable is not connected."
        return try { p.write((text.trimEnd('\r', '\n') + "\r").toByteArray(Charsets.UTF_8), 2000); "ok" } catch (e: Exception) { "Could not send: " + (e.message ?: "") }
    }

    private fun closePort() {
        try { io?.stop() } catch (_: Exception) { }
        io = null
        try { port?.close() } catch (_: Exception) { }
        port = null
    }

    fun close() { wantedKey = ""; prefs.edit().remove("key").apply(); closePort(); set("idle", "Disconnected") }
}
