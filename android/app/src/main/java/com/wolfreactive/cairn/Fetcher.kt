// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors

/**
 * Wi-Fi jobs: small online lookups (weather, earthquakes, the list of releases) and large downloads (offline maps,
 * update packages). Only a short list of well-known hosts is allowed, and only over https.
 */
class Fetcher(private val hub: Hub, private val store: Store) {
    private val pool = Executors.newFixedThreadPool(3)
    private val running = ConcurrentHashMap<String, Boolean>()      // download name -> keep going?
    private val ua = "CAIRN-Station/" + BuildInfo.VERSION + " (Wolf Reactive Systems; +https://github.com/propertymonitor-web/lodestar-cairn)"

    fun allowed(u: String): Boolean {
        val url = try { URL(u) } catch (_: Exception) { return false }
        if (url.protocol != "https") return false
        val h = url.host.lowercase()
        return h == "api.weather.gov" || h == "earthquake.usgs.gov" || h == "api.github.com" || h == "github.com" || h.endsWith(".githubusercontent.com")
    }

    private fun open(start: String, extra: Map<String, String> = emptyMap()): HttpURLConnection {
        var u = start
        for (hop in 0..5) {
            if (!allowed(u)) throw java.io.IOException("address not allowed: " + URL(u).host)
            val c = URL(u).openConnection() as HttpURLConnection
            c.instanceFollowRedirects = false
            c.connectTimeout = 15000; c.readTimeout = 30000
            c.setRequestProperty("User-Agent", ua)
            c.setRequestProperty("Accept", "application/geo+json, application/vnd.github+json, application/json, */*")
            for ((k, v) in extra) c.setRequestProperty(k, v)
            val code = c.responseCode
            if (code in 301..303 || code == 307 || code == 308) {
                val loc = c.getHeaderField("Location") ?: throw java.io.IOException("redirect without a place to go")
                u = URL(URL(u), loc).toString(); c.disconnect(); continue
            }
            return c
        }
        throw java.io.IOException("too many redirects")
    }

    /** A small lookup. The answer is kept in the cache under [key]; the page is told when it is ready. */
    fun get(key: String, url: String) {
        pool.execute {
            val o = JSONObject().put("key", key)
            try {
                val c = open(url)
                val code = c.responseCode
                if (code !in 200..299) { o.put("ok", false).put("status", code).put("msg", "The service answered " + code + "."); c.disconnect(); hub.emit("http", o); return@execute }
                val body = c.inputStream.use { ins ->
                    val out = java.io.ByteArrayOutputStream(); val buf = ByteArray(8192); var total = 0
                    while (true) { val r = ins.read(buf); if (r <= 0) break; total += r; if (total > 3_000_000) throw java.io.IOException("answer too large"); out.write(buf, 0, r) }
                    String(out.toByteArray(), Charsets.UTF_8)
                }
                c.disconnect()
                store.cachePut(key, body)
                o.put("ok", true).put("status", code).put("at", System.currentTimeMillis())
            } catch (e: Exception) {
                o.put("ok", false).put("msg", e.message ?: e.javaClass.simpleName)
            }
            hub.emit("http", o)
        }
    }

    /** A big file, saved in maps/ (kind "map") or downloads/ (kind "release"). Resumes if it was interrupted. */
    fun download(kind: String, rawName: String, url: String): String {
        val name = store.cleanName(rawName) ?: return "bad name"
        if (kind == "map" && !name.endsWith(".pmtiles")) return "A map file must end in .pmtiles"
        if (kind != "map" && kind != "release") return "bad kind"
        if (!allowed(url)) return "That address is not allowed."
        if (running.putIfAbsent(name, true) != null) return "Already downloading"
        val dir = if (kind == "map") store.mapsDir() else File(store.mapsDir().parentFile, "downloads")
        pool.execute {
            val ev = JSONObject().put("name", name).put("kind", kind)
            val part = File(dir, "$name.part")
            try {
                val have = if (part.isFile) part.length() else 0L
                val c = open(url, if (have > 0) mapOf("Range" to "bytes=$have-") else emptyMap())
                val code = c.responseCode
                if (code != 200 && code != 206) throw java.io.IOException("The server answered $code.")
                val append = code == 206 && have > 0
                val len = c.contentLengthLong
                val total = if (len < 0) -1L else (if (append) have + len else len)
                var got = if (append) have else 0L
                var lastEmit = 0L
                FileOutputStream(part, append).use { out ->
                    c.inputStream.use { ins ->
                        val buf = ByteArray(64 * 1024)
                        while (true) {
                            if (running[name] != true) throw java.io.IOException("cancelled")
                            val r = ins.read(buf); if (r <= 0) break
                            out.write(buf, 0, r); got += r
                            val t = System.currentTimeMillis()
                            if (t - lastEmit > 700) { lastEmit = t; hub.emit("dl", JSONObject(ev.toString()).put("state", "run").put("got", got).put("total", total)) }
                        }
                    }
                }
                c.disconnect()
                if (total > 0 && got < total) throw java.io.IOException("The connection dropped. Start the download again and it will carry on.")
                val dest = File(dir, name)
                if (dest.exists()) dest.delete()
                if (!part.renameTo(dest)) throw java.io.IOException("Could not save the file.")
                hub.emit("dl", JSONObject(ev.toString()).put("state", "done").put("got", got).put("total", got))
            } catch (e: Exception) {
                val cancelled = e.message == "cancelled"
                if (cancelled) part.delete()
                hub.emit("dl", JSONObject(ev.toString()).put("state", if (cancelled) "cancelled" else "error").put("msg", e.message ?: "download failed"))
            } finally { running.remove(name) }
        }
        return "ok"
    }

    fun cancel(name: String) { if (running.containsKey(name)) running[name] = false }
}

object BuildInfo { const val VERSION = "1.3.0" }
