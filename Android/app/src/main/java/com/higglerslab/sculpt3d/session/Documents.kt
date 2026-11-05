package com.higglerslab.sculpt3d.session

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.higglerslab.sculpt3d.core.SculptNative
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** A recently opened document on the welcome screen. */
data class RecentDocument(val uri: Uri, val name: String, val time: Long)

/**
 * The open documents (one [DocumentSession] each), the active one, recent
 * documents and the MCP server. Lives for the process (configuration changes
 * don't recreate the activity, and documents survive the activity anyway).
 */
object Documents {
    private const val TAG = "Sculpt3D"
    const val MCP_PORT = 7317
    private lateinit var app: Context
    private val main = Handler(Looper.getMainLooper())

    val sessions = mutableStateListOf<DocumentSession>()
    var active: DocumentSession? by mutableStateOf(null)
        private set
    val recents = mutableStateListOf<RecentDocument>()
    var mcpRunning by mutableStateOf(false)
        private set
    var mcpError: String? by mutableStateOf(null)
        private set
    /** Set by the activity: shows a document and wires its UI handler. */
    var onSessionCreated: ((DocumentSession) -> Unit)? = null

    private val prefs by lazy { app.getSharedPreferences("sculpt3d", Context.MODE_PRIVATE) }

    fun init(context: Context) {
        if (::app.isInitialized) return
        app = context.applicationContext
        loadRecents()
        SculptNative.mcpSetNewDocumentHandler { name ->
            // Called on the UI thread from sculpt_mcp_drain.
            val s = newDocument(name.ifBlank { nextUntitled() })
            s.handle
        }
        if (prefs.getBoolean("mcpEnabled", false)) startMcp()
        // App-wide display formats persist (API.md format.set).
    }

    fun nextUntitled(): String {
        val names = sessions.map { it.displayName }.toSet()
        if ("Untitled" !in names) return "Untitled"
        var i = 2
        while ("Untitled $i" in names) i++
        return "Untitled $i"
    }

    fun newDocument(name: String = nextUntitled()): DocumentSession {
        val s = DocumentSession(name)
        restoreFormats(s)
        sessions.add(s)
        SculptNative.mcpRegister(s.handle)
        onSessionCreated?.invoke(s)
        activate(s)
        return s
    }

    fun activate(s: DocumentSession) {
        active = s
        SculptNative.mcpActivate(s.handle)
        s.refresh(force = true)
    }

    fun close(s: DocumentSession) {
        val i = sessions.indexOf(s)
        if (i < 0) return
        sessions.removeAt(i)
        if (active == s) active = sessions.getOrNull(i.coerceAtMost(sessions.size - 1))
        active?.let { SculptNative.mcpActivate(it.handle) }
        s.renderTarget = null
        SculptNative.mcpUnregister(s.handle)
        // Let the GL view showing it detach (its GL thread exits) before freeing the core document.
        main.postDelayed({ s.destroy() }, 800)
    }

    /** Opens a `.sculpt3d` document from a content or file Uri. Returns an error message. */
    fun open(uri: Uri): String? {
        sessions.firstOrNull { it.uri == uri }?.let { activate(it); return null }
        val bytes = try {
            app.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return "Couldn't read the file"
        } catch (t: Throwable) {
            return "Couldn't open the file: ${t.message}"
        }
        val name = displayName(uri).removeSuffix(".sculpt3d").removeSuffix(".o3d")
        val s = DocumentSession(name)
        restoreFormats(s)
        s.load(bytes)?.let { err ->
            s.destroy()
            return err
        }
        s.uri = uri
        if (uri.scheme == "file") s.autosaveFile = uri.path?.let(::File)
        s.markSaved()
        sessions.add(s)
        SculptNative.mcpRegister(s.handle)
        onSessionCreated?.invoke(s)
        activate(s)
        addRecent(uri, name)
        return null
    }

    fun displayName(uri: Uri): String {
        if (uri.scheme == "file") return uri.lastPathSegment ?: "Untitled"
        return try {
            app.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0) else null
            } ?: uri.lastPathSegment ?: "Untitled"
        } catch (_: Throwable) {
            uri.lastPathSegment ?: "Untitled"
        }
    }

    /** Writes the document to its Uri (or its autosave file). Returns an error message. */
    fun save(s: DocumentSession, to: Uri? = s.uri): String? {
        val target = to ?: return "No location"
        return try {
            val bytes = s.save()
            app.contentResolver.openOutputStream(target, "wt")?.use { it.write(bytes) } ?: return "Couldn't write the file"
            s.markSaved()
            if (to != s.uri) {
                // A real location replaces the autosave copy of an untitled document.
                s.autosaveFile?.let { f ->
                    if (f.parentFile?.name == "Autosave") {
                        recents.firstOrNull { it.uri == Uri.fromFile(f) }?.let(::removeRecent)
                        f.delete()
                    }
                }
                s.autosaveFile = null
                s.uri = to
                val name = displayName(to).removeSuffix(".sculpt3d")
                s.setName(name)
            }
            addRecent(target, s.displayName)
            null
        } catch (t: Throwable) {
            Log.e(TAG, "Save failed", t)
            "Couldn't save: ${t.message}"
        }
    }

    /** Saves every dirty document: to its Uri, or untitled ones to app storage (listed under Recent). */
    fun autosaveAll() {
        for (s in sessions) {
            if (!s.isDirty) continue
            val uri = s.uri
            if (uri != null) {
                save(s, uri)
            } else {
                try {
                    val dir = File(app.filesDir, "Autosave").apply { mkdirs() }
                    val f = s.autosaveFile ?: File(dir, "${s.displayName}-${System.currentTimeMillis()}.sculpt3d")
                    f.writeBytes(s.save())
                    s.autosaveFile = f
                    addRecent(Uri.fromFile(f), s.displayName)
                    // Untitled documents stay dirty: they still want a real location (Save As).
                } catch (t: Throwable) {
                    Log.e(TAG, "Autosave failed", t)
                }
            }
        }
    }

    // ---- Recents ----

    private fun loadRecents() {
        recents.clear()
        val raw = prefs.getString("recents", "[]") ?: "[]"
        try {
            val a = JSONArray(raw)
            for (i in 0 until a.length()) {
                val o = a.getJSONObject(i)
                recents.add(RecentDocument(Uri.parse(o.getString("uri")), o.getString("name"), o.optLong("time")))
            }
        } catch (_: Throwable) {
        }
    }

    fun addRecent(uri: Uri, name: String) {
        if (uri.scheme == "content") {
            try {
                app.contentResolver.takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
                )
            } catch (_: Throwable) {
                try {
                    app.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                } catch (_: Throwable) {
                }
            }
        }
        recents.removeAll { it.uri == uri }
        recents.add(0, RecentDocument(uri, name, System.currentTimeMillis()))
        while (recents.size > 20) recents.removeAt(recents.size - 1)
        val a = JSONArray()
        recents.forEach { a.put(JSONObject().put("uri", it.uri.toString()).put("name", it.name).put("time", it.time)) }
        prefs.edit().putString("recents", a.toString()).apply()
    }

    fun removeRecent(r: RecentDocument) {
        recents.remove(r)
        val a = JSONArray()
        recents.forEach { a.put(JSONObject().put("uri", it.uri.toString()).put("name", it.name).put("time", it.time)) }
        prefs.edit().putString("recents", a.toString()).apply()
    }

    // ---- App-wide display formats (format.set persists and restores at launch) ----

    fun setFormat(footInch: String? = null, degrees: String? = null) {
        val e = prefs.edit()
        footInch?.let { e.putString("footInch", it) }
        degrees?.let { e.putString("degrees", it) }
        e.apply()
        for (s in sessions) restoreFormats(s)
    }

    private fun restoreFormats(s: DocumentSession) {
        val f = prefs.getString("footInch", null)
        val d = prefs.getString("degrees", null)
        if (f != null || d != null) {
            val args = buildList {
                f?.let { add("footInch" to it) }
                d?.let { add("degrees" to it) }
            }
            s.command("format.set", *args.toTypedArray())
        }
    }

    // ---- MCP ----

    fun startMcp() {
        val err = SculptNative.mcpStart(MCP_PORT) { main.post { SculptNative.mcpDrain() } }
        mcpError = err
        mcpRunning = SculptNative.mcpRunning()
        if (mcpRunning) {
            sessions.forEach { SculptNative.mcpRegister(it.handle) }
            active?.let { SculptNative.mcpActivate(it.handle) }
        }
        prefs.edit().putBoolean("mcpEnabled", true).apply()
    }

    fun stopMcp() {
        SculptNative.mcpStop()
        mcpRunning = SculptNative.mcpRunning()
        prefs.edit().putBoolean("mcpEnabled", false).apply()
    }

    // ---- Small preferences used by the UI ----

    fun prefString(key: String, def: String? = null): String? = prefs.getString(key, def)
    fun setPrefString(key: String, value: String?) = prefs.edit().putString(key, value).apply()
}
