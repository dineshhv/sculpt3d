package com.higglerslab.sculpt3d.session

import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.higglerslab.sculpt3d.core.SculptNative
import com.higglerslab.sculpt3d.model.Annotations
import com.higglerslab.sculpt3d.model.AppState
import com.higglerslab.sculpt3d.model.CoreJson
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import java.util.concurrent.atomic.AtomicBoolean

private const val TAG = "Sculpt3D"

/** Something the core asked the front end to do (`"ui"` in a reply or `state.uiRequest`). */
fun interface UiActionHandler {
    fun handle(session: DocumentSession, action: String)
}

/** Converts Kotlin values to JSON for commands. */
fun jsonOf(v: Any?): JsonElement = when (v) {
    null -> JsonNull
    is JsonElement -> v
    is String -> JsonPrimitive(v)
    is Number -> JsonPrimitive(v)
    is Boolean -> JsonPrimitive(v)
    is Map<*, *> -> JsonObject(v.entries.associate { it.key.toString() to jsonOf(it.value) })
    is Iterable<*> -> JsonArray(v.map { jsonOf(it) })
    is Array<*> -> JsonArray(v.map { jsonOf(it) })
    is FloatArray -> JsonArray(v.map { JsonPrimitive(it) })
    is DoubleArray -> JsonArray(v.map { JsonPrimitive(it) })
    is IntArray -> JsonArray(v.map { JsonPrimitive(it) })
    else -> JsonPrimitive(v.toString())
}

/** A command reply: `{"ok": true, …}` or `{"ok": false, "error": "…"}`. */
class Reply(val json: JsonObject) {
    val ok get() = json["ok"]?.jsonPrimitive?.booleanOrNull == true
    val error get() = (json["error"] as? JsonPrimitive)?.contentOrNull
    val ui get() = (json["ui"] as? JsonPrimitive)?.contentOrNull
    fun string(key: String) = (json[key] as? JsonPrimitive)?.contentOrNull
    override fun toString() = json.toString()
}

/**
 * One open document: the native handle, its state snapshot and annotations
 * (Compose state, updated on the main thread from the core's listener), and
 * the GL view currently showing it.
 */
class DocumentSession(name: String) {
    val handle: Long = SculptNative.docCreate(name)
    private val main = Handler(Looper.getMainLooper())
    private val refreshPosted = AtomicBoolean(false)
    @Volatile private var destroyed = false

    var state by mutableStateOf(AppState())
        private set
    var annotations by mutableStateOf(Annotations())
        private set

    /** Where the document lives (SAF content Uri or a file Uri); null until saved. */
    var uri: Uri? by mutableStateOf(null)
    var displayName: String by mutableStateOf(name)
        private set
    /** The file that autosaves land in while the document has no Uri. */
    var autosaveFile: java.io.File? = null

    /** The GL view drawing this document (set on attach, cleared on detach). */
    @Volatile var renderTarget: RenderTarget? = null
        set(value) {
            field = value
            value?.requestRender()
        }

    var uiHandler: UiActionHandler? = null
    private var lastRevision = -1L
    private var lastViewRevision = -1L
    private var handledUiSerial = 0L

    /** Implemented by the GL view: render on demand and run code on its GL thread. */
    interface RenderTarget {
        fun requestRender()
        fun queueOnGl(block: () -> Unit)
    }

    init {
        SculptNative.docSetListener(handle) {
            // Any thread (also while MCP waits for a screenshot): never block.
            renderTarget?.requestRender()
            if (refreshPosted.compareAndSet(false, true)) main.post { refreshPosted.set(false); refresh() }
        }
        refresh(force = true)
    }

    fun setName(name: String) {
        displayName = name
        SculptNative.docSetName(handle, name)
        refresh(force = true)
    }

    /** Re-reads the state when the document revision moved, and the annotations when the view revision did. */
    fun refresh(force: Boolean = false) {
        if (destroyed) return
        val rev = SculptNative.docRevision(handle)
        if (force || rev != lastRevision) {
            lastRevision = rev
            try {
                val s = CoreJson.decodeFromString(AppState.serializer(), SculptNative.docState(handle))
                state = s
                val req = s.uiRequest
                if (req.serial > handledUiSerial && req.action.isNotEmpty()) {
                    handledUiSerial = req.serial
                    command("ui.clearRequest")
                    uiHandler?.handle(this, req.action)
                }
            } catch (t: Throwable) {
                Log.e(TAG, "State decode failed", t)
            }
        }
        refreshAnnotations()
    }

    fun refreshAnnotations(force: Boolean = false) {
        if (destroyed) return
        val vrev = SculptNative.viewRevision(handle)
        if (force || vrev != lastViewRevision) {
            lastViewRevision = vrev
            try {
                annotations = CoreJson.decodeFromString(Annotations.serializer(), SculptNative.viewAnnotations(handle))
            } catch (t: Throwable) {
                Log.e(TAG, "Annotations decode failed", t)
            }
        }
    }

    /** Sends `{"cmd": name, …args}`. Replies with `ui` are passed to the UI handler. */
    fun command(name: String, vararg args: Pair<String, Any?>): Reply {
        val body = JsonObject(mapOf("cmd" to JsonPrimitive(name)) + args.associate { it.first to jsonOf(it.second) })
        return send(body)
    }

    fun send(body: JsonObject): Reply {
        if (destroyed) return Reply(JsonObject(mapOf("ok" to JsonPrimitive(false))))
        val raw = SculptNative.docCommand(handle, body.toString())
        val reply = try {
            Reply(CoreJson.parseToJsonElement(raw).jsonObject)
        } catch (t: Throwable) {
            Reply(JsonObject(mapOf("ok" to JsonPrimitive(false), "error" to JsonPrimitive(raw))))
        }
        if (!reply.ok) Log.w(TAG, "${body["cmd"]} -> ${reply.error}")
        reply.ui?.let { action -> main.post { uiHandler?.handle(this, action) } }
        refresh()
        return reply
    }

    /** Runs an argument-free action (`state.actions`). */
    fun action(id: String) = command(id)
    fun runTool(id: String) = command("tool.run", "id" to id)

    // ---- Viewport input (queued to the GL thread so it stays ordered with rendering) ----

    fun onGl(block: () -> Unit) {
        val t = renderTarget
        if (t != null) t.queueOnGl(block) else block()
    }

    fun key(key: String, mods: Int): Boolean = SculptNative.viewKey(handle, key, mods).also { refresh() }

    // ---- Files ----

    fun save(): ByteArray = SculptNative.docSave(handle)
    fun load(bytes: ByteArray): String? = SculptNative.docLoad(handle, bytes).also { refresh(force = true) }
    fun markSaved() {
        SculptNative.docMarkSaved(handle)
        refresh(force = true)
    }
    val isDirty get() = SculptNative.docIsDirty(handle)

    fun destroy() {
        if (destroyed) return
        destroyed = true
        renderTarget = null
        SculptNative.docSetListener(handle, null)
        SculptNative.mcpUnregister(handle)
        SculptNative.docDestroy(handle)
    }
}
