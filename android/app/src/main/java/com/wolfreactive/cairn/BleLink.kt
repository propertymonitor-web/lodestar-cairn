// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.os.Build
import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.util.UUID

/**
 * Bluetooth link to the CAIRN companion radio (a MeshCore companion: Nordic UART service, static-PIN pairing).
 * Text lines travel inside frame type 0xC8 (see Tunnel). The link re-opens by itself if it drops, until the person
 * asks to disconnect.
 */
@SuppressLint("MissingPermission")
@Suppress("DEPRECATION")
class BleLink(private val ctx: Context, private val hub: Hub) {
    companion object {
        val SERVICE: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val RX: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")   // tablet writes here
        val TX: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")   // radio notifies here
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    private val main = Handler(Looper.getMainLooper())
    private val prefs: SharedPreferences = ctx.getSharedPreferences("cairn-ble", Context.MODE_PRIVATE)
    private val adapter get() = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    @Volatile var state = "idle"; private set
    private var addr = ""
    private var name = ""
    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var wanted = false
    private var scanning = false
    private var retry = 0
    private val splitter = LineSplitter()
    private val queue = ArrayDeque<ByteArray>()
    private var writing = false
    private var bondReceiver: BroadcastReceiver? = null

    fun lastAddr(): String = prefs.getString("addr", "") ?: ""
    fun lastName(): String = prefs.getString("name", "") ?: ""

    private fun set(s: String, msg: String = "") {
        state = s
        hub.emit("ble", JSONObject().put("state", s).put("msg", msg).put("addr", addr).put("name", name), remember = true)
    }

    fun hasPermission(): Boolean {
        fun ok(p: String) = ctx.checkSelfPermission(p) == android.content.pm.PackageManager.PERMISSION_GRANTED
        return if (Build.VERSION.SDK_INT >= 31) ok("android.permission.BLUETOOTH_SCAN") && ok("android.permission.BLUETOOTH_CONNECT")
        else ok("android.permission.ACCESS_FINE_LOCATION")
    }

    // ---- finding the radio --------------------------------------------------------------------
    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, r: ScanResult) {
            val dev = r.device
            val nm = (try { dev.name } catch (_: Exception) { null }) ?: r.scanRecord?.deviceName ?: ""
            val uart = r.scanRecord?.serviceUuids?.any { it.uuid == SERVICE } == true
            if (!uart && !nm.startsWith("MeshCore", true)) return
            hub.emit("bleDev", JSONObject().put("addr", dev.address).put("name", if (nm.isEmpty()) "MeshCore radio" else nm).put("rssi", r.rssi).put("bonded", dev.bondState == BluetoothDevice.BOND_BONDED))
        }
        override fun onScanFailed(errorCode: Int) { scanning = false; hub.emit("bleDev", JSONObject().put("error", "Scan failed (code $errorCode).")) }
    }

    fun scan(on: Boolean): String {
        val ad = adapter ?: return "This tablet has no Bluetooth."
        if (!ad.isEnabled) return "Bluetooth is switched off. Turn it on in Android settings."
        if (!hasPermission()) return "needperm"
        val sc = ad.bluetoothLeScanner ?: return "Bluetooth is not ready yet."
        if (on && !scanning) {
            // radios already paired with this tablet are listed straight away
            try { for (d in ad.bondedDevices) if ((d.name ?: "").startsWith("MeshCore", true)) hub.emit("bleDev", JSONObject().put("addr", d.address).put("name", d.name).put("rssi", 0).put("bonded", true)) } catch (_: Exception) { }
            try { sc.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCb); scanning = true } catch (e: Exception) { return "Could not scan: " + e.message }
            main.postDelayed({ scan(false) }, 25000)
        } else if (!on && scanning) {
            try { sc.stopScan(scanCb) } catch (_: Exception) { }
            scanning = false
        }
        return "ok"
    }

    // ---- connecting -----------------------------------------------------------------------------
    fun open(address: String, deviceName: String): String {
        val ad = adapter ?: return "This tablet has no Bluetooth."
        if (!ad.isEnabled) return "Bluetooth is switched off. Turn it on in Android settings."
        if (!hasPermission()) return "needperm"
        scan(false)
        addr = address; name = deviceName; wanted = true; retry = 0
        prefs.edit().putString("addr", address).putString("name", deviceName).apply()
        val dev = try { ad.getRemoteDevice(address) } catch (_: Exception) { return "That is not a Bluetooth address." }
        if (dev.bondState == BluetoothDevice.BOND_BONDED) connect(dev) else pair(dev)
        return "ok"
    }

    /** First time with this radio: Android asks for the PIN shown on the radio's own screen. */
    private fun pair(dev: BluetoothDevice) {
        set("pairing", "Android will ask for a PIN. Type the PIN shown on the radio's screen.")
        val rcv = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) {
                val d: BluetoothDevice? = if (Build.VERSION.SDK_INT >= 33) i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java) else i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                if (d?.address != dev.address) return
                when (i.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)) {
                    BluetoothDevice.BOND_BONDED -> { stopBondWatch(); if (wanted) connect(dev) }
                    BluetoothDevice.BOND_NONE -> if (i.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, -1) == BluetoothDevice.BOND_BONDING) {
                        stopBondWatch(); set("error", "Pairing did not finish (wrong PIN?). Try again."); wanted = false
                    }
                }
            }
        }
        bondReceiver = rcv
        if (Build.VERSION.SDK_INT >= 33) ctx.registerReceiver(rcv, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED), Context.RECEIVER_EXPORTED)
        else ctx.registerReceiver(rcv, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED))
        if (!dev.createBond()) { stopBondWatch(); set("error", "Android could not start pairing."); wanted = false }
        main.postDelayed({ if (state == "pairing") { stopBondWatch(); set("error", "Pairing timed out. Try again."); wanted = false } }, 90000)
    }

    private fun stopBondWatch() { bondReceiver?.let { try { ctx.unregisterReceiver(it) } catch (_: Exception) { } }; bondReceiver = null }

    private fun connect(dev: BluetoothDevice) {
        set("connecting", "Connecting to $name")
        closeGatt()
        splitter.clear(); queue.clear(); writing = false
        gatt = if (Build.VERSION.SDK_INT >= 23) dev.connectGatt(ctx, false, cb, BluetoothDevice.TRANSPORT_LE) else dev.connectGatt(ctx, false, cb)
    }

    private fun closeGatt() { try { gatt?.disconnect() } catch (_: Exception) { }; try { gatt?.close() } catch (_: Exception) { }; gatt = null; rx = null }

    fun close() {
        wanted = false
        scan(false); stopBondWatch(); closeGatt()
        if (state != "idle") set("idle", "Disconnected")
    }

    private fun lost(why: String) {
        closeGatt()
        if (!wanted) { set("closed", why); return }
        set("closed", why + " Reconnecting...")
        retry++
        val wait = minOf(3000L * retry, 15000L)
        main.postDelayed({ if (wanted && state == "closed") { val d = try { adapter?.getRemoteDevice(addr) } catch (_: Exception) { null }; if (d != null) connect(d) } }, wait)
    }

    private val cb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g !== gatt) return
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                main.post { if (g === gatt && !g.requestMtu(185)) g.discoverServices() }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                main.post { if (g === gatt) lost(if (status == 0) "The radio disconnected." else "The Bluetooth link dropped (code $status).") }
            }
        }
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) { if (g === gatt) main.postDelayed({ if (g === gatt) g.discoverServices() }, 300) }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (g !== gatt) return
            val svc = g.getService(SERVICE)
            val txc = svc?.getCharacteristic(TX)
            val rxc = svc?.getCharacteristic(RX)
            if (status != BluetoothGatt.GATT_SUCCESS || txc == null || rxc == null) { main.post { wanted = false; closeGatt(); set("error", "That device is not a MeshCore radio.") }; return }
            rx = rxc
            g.setCharacteristicNotification(txc, true)
            val d = txc.getDescriptor(CCCD)
            if (d == null) { main.post { wanted = false; closeGatt(); set("error", "The radio did not offer notifications.") }; return }
            if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            else { d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; g.writeDescriptor(d) }
        }
        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            if (g !== gatt) return
            main.post {
                if (g !== gatt) return@post
                if (status == BluetoothGatt.GATT_SUCCESS) { retry = 0; set("connected", "Connected to $name") }
                else { wanted = false; closeGatt(); set("error", "The radio refused the link (code $status). Remove it from Android's Bluetooth list and pair again.") }
            }
        }
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) { main.post { writing = false; pump() } }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) { got(c.value ?: return) }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) { got(value) }
    }

    private fun got(frame: ByteArray) {
        val t = Tunnel.textBytes(frame) ?: return        // every other kind of frame is the phone app's business
        val lines = splitter.feed(t)
        if (lines.isNotEmpty()) hub.emit("bleData", JSONObject().put("t", lines.joinToString("\n")))
    }

    // ---- sending --------------------------------------------------------------------------------
    fun write(line: String): String {
        if (state != "connected") return "The radio is not connected."
        val f = Tunnel.lineFrame(line) ?: return "That line is empty or too long for the radio."
        main.post { if (queue.size > 30) queue.removeFirst(); queue.addLast(f); pump() }
        return "ok"
    }

    private fun pump() {
        if (writing) return
        val g = gatt ?: return
        val c = rx ?: return
        val f = queue.removeFirstOrNull() ?: return
        writing = true
        val type = if ((c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE) != 0) BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT else BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        val ok = try {
            if (Build.VERSION.SDK_INT >= 33) g.writeCharacteristic(c, f, type) == BluetoothGatt.GATT_SUCCESS
            else { c.writeType = type; c.value = f; g.writeCharacteristic(c) }
        } catch (_: Exception) { false }
        if (!ok) writing = false
        else main.postDelayed({ if (writing) { writing = false; pump() } }, 2500)   // never stall on a lost acknowledgement
    }
}
