package com.higglerslab.sculpt3d.core

/**
 * Kotlin binding of AppCore's C API (AppCore/include/sculpt/sculpt_c.h), via
 * app/src/main/cpp/sculpt_jni.cpp. Document handles are opaque longs.
 * State and commands are JSON (see AppCore/API.md).
 */
object SculptNative {
    init {
        System.loadLibrary("sculpt")
        System.loadLibrary("sculpt_jni")
    }

    const val PHASE_DOWN = 0
    const val PHASE_MOVE = 1
    const val PHASE_UP = 2
    const val PHASE_CANCEL = 3
    const val PHASE_HOVER = 4
    const val PHASE_EXIT = 5

    const val POINTER_TOUCH = 0
    const val POINTER_MOUSE = 1
    const val POINTER_PEN = 2

    const val BUTTON_PRIMARY = 0
    const val BUTTON_SECONDARY = 1
    const val BUTTON_MIDDLE = 2

    const val MOD_SHIFT = 1
    const val MOD_CONTROL = 2
    const val MOD_ALT = 4

    /** Called by the core when MCP needs a new window; returns the new document handle. */
    fun interface NewDocumentHandler {
        fun newDocument(name: String): Long
    }

    external fun version(): String

    // Documents
    external fun docCreate(name: String): Long
    external fun docDestroy(doc: Long)
    /** Returns null on success or an error message. */
    external fun docLoad(doc: Long, data: ByteArray): String?
    external fun docSave(doc: Long): ByteArray
    external fun docIsDirty(doc: Long): Boolean
    external fun docMarkSaved(doc: Long)
    external fun docSetName(doc: Long, name: String)

    // State and commands
    external fun docRevision(doc: Long): Long
    /** `listener.run()` is called on a native thread after any change. */
    external fun docSetListener(doc: Long, listener: Runnable?)
    external fun docState(doc: Long): String
    external fun docCommand(doc: Long, json: String): String

    // Files
    @Throws(java.io.IOException::class)
    external fun docExport(doc: Long, format: String, optionsJson: String?): ByteArray
    external fun docImportPath(doc: Long, path: String): String?
    /** `decal`: false = reference image, true = decal. See docImportImageAs for terrain height maps. */
    external fun docImportImage(doc: Long, data: ByteArray, name: String, decal: Boolean): String?
    /** `mode`: 0 reference image, 1 decal on the selected face, 2 terrain from a height map. */
    external fun docImportImageAs(doc: Long, data: ByteArray, name: String, mode: Int): String?

    // Viewport — call on the GL thread
    external fun viewGlInit(doc: Long)
    external fun viewGlRelease(doc: Long)
    external fun viewResize(doc: Long, widthPx: Int, heightPx: Int, scale: Float)
    external fun viewRender(doc: Long)
    external fun viewNeedsRedraw(doc: Long): Boolean
    external fun viewSnapshot(doc: Long, widthPx: Int, heightPx: Int): ByteArray?
    /** The snapshot as PNG bytes (GL thread); null without a GL context. */
    external fun viewSnapshotPng(doc: Long, widthPx: Int, heightPx: Int): ByteArray?
    /** Moves with anything drawn or annotated, camera included (any thread). */
    external fun viewRevision(doc: Long): Long
    /** Labels over the viewport in dp, plus the camera (API.md "Annotations"). */
    external fun viewAnnotations(doc: Long): String

    // Viewport input — coordinates in dp, origin top-left
    external fun viewPointer(doc: Long, id: Int, type: Int, button: Int, phase: Int, x: Float, y: Float, modifiers: Int)
    external fun viewOrbit(doc: Long, dx: Float, dy: Float)
    external fun viewPan(doc: Long, dx: Float, dy: Float)
    external fun viewZoom(doc: Long, factor: Float, x: Float, y: Float)
    external fun viewKey(doc: Long, key: String, modifiers: Int): Boolean

    // MCP server
    external fun mcpStart(port: Int, post: Runnable): String?
    external fun mcpStop()
    external fun mcpRunning(): Boolean
    external fun mcpDrain()
    external fun mcpRegister(doc: Long)
    external fun mcpUnregister(doc: Long)
    external fun mcpActivate(doc: Long)
    external fun mcpSetNewDocumentHandler(handler: NewDocumentHandler?)
}
