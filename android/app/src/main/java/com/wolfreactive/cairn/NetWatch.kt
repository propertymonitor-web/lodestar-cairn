// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import org.json.JSONObject

/** Tells the dashboard whether the tablet is on Wi-Fi and whether the internet really works (Android checks that, not just the signal). */
class NetWatch(private val ctx: Context, private val hub: Hub) {
    private val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    @Volatile var wifi = false; private set
    @Volatile var online = false; private set
    private var first = true

    private val cb = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) { refresh() }
        override fun onLost(network: Network) { refresh() }
        override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) { refresh() }
    }

    fun start() {
        try { cm.registerDefaultNetworkCallback(cb) } catch (_: Exception) { }
        refresh()
    }

    fun refresh() {
        var w = false; var o = false
        try {
            val n = cm.activeNetwork
            val c = if (n != null) cm.getNetworkCapabilities(n) else null
            if (c != null) {
                w = c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                o = c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) && c.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
            }
        } catch (_: Exception) { }
        val changed = first || w != wifi || o != online
        first = false
        wifi = w; online = o
        if (changed) hub.emit("net", state(), remember = true)
    }

    fun state(): JSONObject = JSONObject().put("wifi", wifi).put("online", online)
}
