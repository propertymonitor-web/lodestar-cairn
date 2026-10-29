// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
// Special thanks to the NTXMESH community, Meshcore Community, and Pat Steinhauer-Scates for their guidance and support.
package com.wolfreactive.cairn

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import android.webkit.JsPromptResult
import android.webkit.JsResult
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.EditText
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.webkit.WebViewAssetLoader
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.io.RandomAccessFile

/**
 * The whole screen is one web page (the CAIRN dashboard, stored inside this app) with the tablet's abilities handed to it
 * through [Bridge]. The app is also offered to Android as a Home app so the tablet starts straight into it.
 */
class MainActivity : Activity() {
    private lateinit var web: WebView
    private lateinit var hub: Hub
    private var chooser: ValueCallback<Array<Uri>>? = null
    private val DOMAIN = "appassets.androidplatform.net"
    private val REQ_BT = 41
    private val REQ_FILE = 42

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        hub = (application as CairnApp).hub
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.setDecorFitsSystemWindows(window, false)

        web = WebView(this)
        setContentView(web)
        web.setBackgroundColor(0xFF03100C.toInt())
        val st = web.settings
        st.javaScriptEnabled = true
        st.domStorageEnabled = true
        st.allowFileAccess = false
        st.allowContentAccess = false
        st.mediaPlaybackRequiresUserGesture = false      // the alert beep must sound without a tap
        st.setSupportZoom(false)
        st.cacheMode = WebSettings.LOAD_DEFAULT
        st.mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW

        val loader = WebViewAssetLoader.Builder()
            .setDomain(DOMAIN)
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .build()

        web.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
                val u = request.url
                if (u.host == DOMAIN && (u.path ?: "").startsWith("/maps/")) return serveMap(request)
                return loader.shouldInterceptRequest(u)
            }
            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                val u = request.url
                if (u.host == DOMAIN) return false
                try { startActivity(Intent(Intent.ACTION_VIEW, u).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) { }
                return true
            }
        }
        web.webChromeClient = object : WebChromeClient() {
            override fun onJsAlert(view: WebView, url: String, message: String, result: JsResult): Boolean {
                AlertDialog.Builder(this@MainActivity).setMessage(message).setPositiveButton("OK") { _, _ -> result.confirm() }.setOnCancelListener { result.confirm() }.show(); return true
            }
            override fun onJsConfirm(view: WebView, url: String, message: String, result: JsResult): Boolean {
                AlertDialog.Builder(this@MainActivity).setMessage(message)
                    .setPositiveButton("OK") { _, _ -> result.confirm() }.setNegativeButton("Cancel") { _, _ -> result.cancel() }.setOnCancelListener { result.cancel() }.show(); return true
            }
            override fun onJsPrompt(view: WebView, url: String, message: String, defaultValue: String?, result: JsPromptResult): Boolean {
                val box = EditText(this@MainActivity); box.setText(defaultValue ?: "")
                AlertDialog.Builder(this@MainActivity).setMessage(message).setView(box)
                    .setPositiveButton("OK") { _, _ -> result.confirm(box.text.toString()) }.setNegativeButton("Cancel") { _, _ -> result.cancel() }.setOnCancelListener { result.cancel() }.show(); return true
            }
            // "Load a map file": the page's file box opens Android's file picker
            override fun onShowFileChooser(view: WebView, cb: ValueCallback<Array<Uri>>, params: FileChooserParams): Boolean {
                chooser?.onReceiveValue(null); chooser = cb
                return try {
                    val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*").putExtra(Intent.EXTRA_ALLOW_MULTIPLE, params.mode == FileChooserParams.MODE_OPEN_MULTIPLE)
                    startActivityForResult(i, REQ_FILE); true
                } catch (_: Exception) { chooser = null; false }
            }
        }
        web.setDownloadListener { _, _, _, _, _ -> }       // saving goes through the Files screen, not browser downloads

        hub.attach(web)
        web.addJavascriptInterface(Bridge(this, hub), "CairnBridge")
        web.loadUrl("https://$DOMAIN/assets/station.html")
        hideBars()
    }

    // ---- offline map files, served to the page with byte ranges (the map reader asks for pieces) ----
    private fun serveMap(req: WebResourceRequest): WebResourceResponse {
        val name = Uri.decode((req.url.path ?: "").removePrefix("/maps/"))
        val f: File? = hub.store.mapFile(name)
        if (f == null) return WebResourceResponse("text/plain", "utf-8", 404, "Not Found", emptyMap(), "no such map".byteInputStream())
        val total = f.length()
        var start = 0L; var end = total - 1; var status = 200
        val range = req.requestHeaders["Range"] ?: req.requestHeaders["range"]
        if (range != null && range.startsWith("bytes=")) {
            val parts = range.removePrefix("bytes=").split("-")
            val a = parts.getOrNull(0)?.toLongOrNull(); val b = parts.getOrNull(1)?.toLongOrNull()
            if (a != null) { start = a; if (b != null) end = minOf(b, total - 1) }
            else if (b != null) { start = maxOf(0L, total - b) }        // "last b bytes"
            if (start > end || start >= total) return WebResourceResponse("text/plain", "utf-8", 416, "Range Not Satisfiable", mapOf("Content-Range" to "bytes */$total"), "".byteInputStream())
            status = 206
        }
        val len = end - start + 1
        val raf = RandomAccessFile(f, "r"); raf.seek(start)
        val ins = object : InputStream() {
            var left = len
            override fun read(): Int { if (left <= 0) return -1; val r = raf.read(); if (r >= 0) left--; return r }
            override fun read(b: ByteArray, off: Int, n: Int): Int {
                if (left <= 0) return -1
                val r = raf.read(b, off, minOf(n.toLong(), left).toInt()); if (r > 0) left -= r; return r
            }
            override fun close() { try { raf.close() } catch (_: Exception) { } }
        }
        val h = HashMap<String, String>()
        h["Accept-Ranges"] = "bytes"; h["Content-Length"] = len.toString(); h["Access-Control-Allow-Origin"] = "*"
        if (status == 206) h["Content-Range"] = "bytes $start-$end/$total"
        return WebResourceResponse("application/octet-stream", null, status, if (status == 206) "Partial Content" else "OK", h, ins)
    }

    fun askBluetoothPermission() {
        val p = if (Build.VERSION.SDK_INT >= 31) arrayOf("android.permission.BLUETOOTH_SCAN", "android.permission.BLUETOOTH_CONNECT") else arrayOf("android.permission.ACCESS_FINE_LOCATION")
        runOnUiThread { requestPermissions(p, REQ_BT) }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_BT) hub.emit("perm", JSONObject().put("ble", hub.ble.hasPermission()))
    }

    @Deprecated("fine for a plain Activity")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_FILE) {
            val out: Array<Uri>? = if (resultCode != RESULT_OK || data == null) null else {
                val clip = data.clipData
                if (clip != null) Array(clip.itemCount) { clip.getItemAt(it).uri } else data.data?.let { arrayOf(it) }
            }
            chooser?.onReceiveValue(out); chooser = null
        }
    }

    fun keepOn(on: Boolean) = runOnUiThread { if (on) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }

    private fun hideBars() {
        val c = WindowInsetsControllerCompat(window, window.decorView)
        c.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        c.hide(WindowInsetsCompat.Type.systemBars())
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) { super.onWindowFocusChanged(hasFocus); if (hasFocus) hideBars() }

    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); hub.usb.autoOpen() }

    override fun onResume() { super.onResume(); hub.net.refresh(); web.onResume(); web.evaluateJavascript("window.cairnNative&&window.cairnNative.resumed&&window.cairnNative.resumed()", null) }

    // The tablet is a dedicated station: Back does nothing, so a stray swipe cannot leave the screen.
    @Deprecated("fine for a plain Activity")
    override fun onBackPressed() { web.evaluateJavascript("window.cairnNative&&window.cairnNative.back&&window.cairnNative.back()", null) }

    override fun onDestroy() { hub.attach(null); web.removeJavascriptInterface("CairnBridge"); web.destroy(); super.onDestroy() }
}
