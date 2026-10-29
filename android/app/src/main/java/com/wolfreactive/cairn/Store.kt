// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.core.content.FileProvider
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors

/**
 * Everything CAIRN keeps on the tablet, under the app's private folder:
 *   logs/       daily logs of what the radio hears and what the repeater says (written by the app as it happens)
 *   reports/    situation reports, field guide copies and other files made on the Files screen
 *   downloads/  files fetched over Wi-Fi (update packages, firmware)
 *   maps/       offline map files (not listed on the Files screen; the Map screen manages them)
 *   cache/      the last answer of each online service, so it can still be shown with no internet
 */
class Store(private val ctx: Context) {
    private val root = File(ctx.filesDir, "cairn").also { it.mkdirs() }
    private val io = Executors.newSingleThreadExecutor()
    val kinds = listOf("logs", "reports", "downloads")

    init { (kinds + listOf("maps", "cache")).forEach { File(root, it).mkdirs() } }

    fun mapsDir() = File(root, "maps")
    fun cacheDir() = File(root, "cache")

    /** Only plain names: letters, digits, dot, dash, underscore and space. Never a path. */
    fun cleanName(n: String): String? {
        val t = n.trim()
        if (t.isEmpty() || t.length > 100 || t.startsWith(".")) return null
        if (!t.all { it.isLetterOrDigit() || it == '.' || it == '-' || it == '_' || it == ' ' }) return null
        return t
    }

    /** "logs/journal-2026-10-28.log" -> the file, or null when the name is not one of ours. */
    fun resolve(rel: String): File? {
        val parts = rel.split('/')
        if (parts.size != 2 || parts[0] !in kinds) return null
        val name = cleanName(parts[1]) ?: return null
        val f = File(File(root, parts[0]), name)
        return if (f.canonicalPath.startsWith(root.canonicalPath + File.separator)) f else null
    }

    fun list(): JSONArray {
        val a = JSONArray()
        val all = ArrayList<Pair<String, File>>()
        for (k in kinds) File(root, k).listFiles()?.filter { it.isFile && !it.name.endsWith(".part") }?.forEach { all.add(k to it) }
        all.sortByDescending { it.second.lastModified() }
        for ((k, f) in all) a.put(JSONObject().put("path", "$k/${f.name}").put("kind", k).put("name", f.name).put("size", f.length()).put("mtime", f.lastModified()))
        return a
    }

    fun read(rel: String, maxBytes: Int = 400_000): String? {
        val f = resolve(rel) ?: return null
        if (!f.isFile) return null
        val len = f.length()
        val take = minOf(len, maxBytes.toLong()).toInt()
        val buf = ByteArray(take)
        f.inputStream().use { ins ->
            if (len > take) ins.skip(len - take)      // a long log: the newest part
            var o = 0
            while (o < take) { val r = ins.read(buf, o, take - o); if (r <= 0) break; o += r }
        }
        return String(buf, Charsets.UTF_8)
    }

    fun write(kind: String, name: String, text: String): String? {
        if (kind !in kinds) return null
        val n = cleanName(name) ?: return null
        val f = File(File(root, kind), n)
        return try { FileOutputStream(f).use { it.write(text.toByteArray(Charsets.UTF_8)) }; "$kind/$n" } catch (_: Exception) { null }
    }

    private val day = SimpleDateFormat("yyyy-MM-dd", Locale.US)
    private val maxLogBytes = 20L * 1024 * 1024

    /** Adds one line to today's log of that kind, for example logs/journal-2026-10-28.log. */
    fun log(kind: String, line: String) {
        val k = kind.filter { it.isLetterOrDigit() }.take(20)
        if (k.isEmpty()) return
        val l = line.take(4000)
        io.execute {
            try {
                val f = File(File(root, "logs"), k + "-" + synchronized(day) { day.format(Date()) } + ".log")
                if (f.length() < maxLogBytes) FileOutputStream(f, true).use { it.write((l + "\n").toByteArray(Charsets.UTF_8)) }
            } catch (_: Exception) { }
        }
    }

    fun delete(rel: String): Boolean = resolve(rel)?.delete() ?: false

    /** Puts a copy where the person can reach it with any file app: Downloads/CAIRN. */
    fun exportToDownloads(rel: String): String? {
        val f = resolve(rel) ?: return null
        if (!f.isFile) return null
        return try {
            val mime = when (f.extension.lowercase()) { "pdf" -> "application/pdf"; "html" -> "text/html"; "csv" -> "text/csv"; "apk" -> "application/vnd.android.package-archive"; else -> "text/plain" }
            if (Build.VERSION.SDK_INT >= 29) {
                val cv = ContentValues().apply {
                    put(MediaStore.Downloads.DISPLAY_NAME, f.name)
                    put(MediaStore.Downloads.MIME_TYPE, mime)
                    put(MediaStore.Downloads.RELATIVE_PATH, "Download/CAIRN")
                }
                val uri = ctx.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv) ?: return null
                ctx.contentResolver.openOutputStream(uri)?.use { out -> f.inputStream().use { it.copyTo(out) } }
                "Download/CAIRN/" + f.name
            } else {
                val dir = File(android.os.Environment.getExternalStoragePublicDirectory(android.os.Environment.DIRECTORY_DOWNLOADS), "CAIRN")
                dir.mkdirs()
                f.copyTo(File(dir, f.name), overwrite = true)
                "Download/CAIRN/" + f.name
            }
        } catch (_: Exception) { null }
    }

    fun uriFor(f: File): Uri = FileProvider.getUriForFile(ctx, ctx.packageName + ".files", f)

    /** The share sheet: email, messaging or any other app the person has. */
    fun share(rel: String): Boolean {
        val f = resolve(rel) ?: return false
        if (!f.isFile) return false
        return try {
            val mime = when (f.extension.lowercase()) { "pdf" -> "application/pdf"; "html" -> "text/html"; "csv" -> "text/csv"; else -> "text/plain" }
            val i = Intent(Intent.ACTION_SEND).apply {
                type = mime
                putExtra(Intent.EXTRA_STREAM, uriFor(f))
                putExtra(Intent.EXTRA_SUBJECT, "CAIRN: " + f.name)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            val c = Intent.createChooser(i, "Send " + f.name).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            ctx.startActivity(c); true
        } catch (_: Exception) { false }
    }

    /** Asks Android to install an update package that was downloaded over Wi-Fi. Android shows its own confirmation. */
    fun installApk(rel: String): String {
        val f = resolve(rel) ?: return "bad name"
        if (!f.isFile || !f.name.endsWith(".apk", true)) return "not an app package"
        return try {
            val i = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uriFor(f), "application/vnd.android.package-archive")
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            ctx.startActivity(i); "ok"
        } catch (e: Exception) { "Android would not open the installer: " + (e.message ?: "unknown") }
    }

    // ---- offline maps ------------------------------------------------------------------------
    fun mapFile(name: String): File? {
        val n = cleanName(name) ?: return null
        if (!n.endsWith(".pmtiles")) return null
        val f = File(mapsDir(), n)
        return if (f.isFile) f else null
    }

    fun mapList(): JSONArray {
        val a = JSONArray()
        mapsDir().listFiles()?.filter { it.isFile && it.name.endsWith(".pmtiles") }?.sortedBy { it.name }?.forEach {
            a.put(JSONObject().put("name", it.name).put("size", it.length()).put("mtime", it.lastModified()))
        }
        return a
    }

    fun mapDelete(name: String): Boolean = mapFile(name)?.delete() ?: false

    // ---- cache of online answers ---------------------------------------------------------------
    fun cachePut(key: String, body: String) {
        val k = key.filter { it.isLetterOrDigit() || it == '-' }.take(40); if (k.isEmpty()) return
        try {
            File(cacheDir(), "$k.json").writeText(body, Charsets.UTF_8)
            File(cacheDir(), "$k.at").writeText(System.currentTimeMillis().toString())
        } catch (_: Exception) { }
    }

    fun cacheGet(key: String): JSONObject? {
        val k = key.filter { it.isLetterOrDigit() || it == '-' }.take(40); if (k.isEmpty()) return null
        return try {
            val b = File(cacheDir(), "$k.json"); if (!b.isFile) return null
            val at = File(cacheDir(), "$k.at").takeIf { it.isFile }?.readText()?.trim()?.toLongOrNull() ?: b.lastModified()
            JSONObject().put("at", at).put("body", b.readText(Charsets.UTF_8))
        } catch (_: Exception) { null }
    }
}
