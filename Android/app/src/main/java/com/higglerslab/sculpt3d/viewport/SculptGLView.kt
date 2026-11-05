package com.higglerslab.sculpt3d.viewport

import android.annotation.SuppressLint
import android.content.Context
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import com.higglerslab.sculpt3d.core.SculptNative
import com.higglerslab.sculpt3d.session.DocumentSession
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.opengles.GL10
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.pow

private const val TAG = "Sculpt3D"

/**
 * The 3D viewport: an ES 3.0 GLSurfaceView the core draws into, rendering on
 * demand (revisions / `sculpt_view_needs_redraw`). Raw pointer events go to
 * the core (`sculpt_view_pointer`), which recognises taps, double taps and
 * one-finger drags; two-finger pan, pinch and twist, the mouse wheel and the
 * hardware keyboard are recognised here (API.md "Viewport input").
 */
@SuppressLint("ViewConstructor")
class SculptGLView(context: Context, val session: DocumentSession) : GLSurfaceView(context), DocumentSession.RenderTarget {
    private val density get() = resources.displayMetrics.density
    private val doc = session.handle
    @Volatile private var surfaceReady = false
    /** Called when a finger or button goes down on the viewport (closes popovers and flyouts). */
    var onTouchDown: (() -> Unit)? = null

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(MsaaConfigChooser())
        preserveEGLContextOnPause = true
        setRenderer(SculptRenderer())
        renderMode = RENDERMODE_WHEN_DIRTY
        isFocusable = true
        isFocusableInTouchMode = true
        // Focus handed back from a text field by the Enter key counts as keyboard
        // navigation, and Android would then grey the whole viewport with its focus highlight.
        defaultFocusHighlightEnabled = false
    }

    // ---- RenderTarget ----

    override fun queueOnGl(block: () -> Unit) {
        if (surfaceReady) queueEvent(block) else block()
    }

    // Hardware keys reach the core only while something holds focus in touch mode: with
    // nothing focused, Android spends the first Enter/arrow/Space on leaving touch mode
    // (focusing the first button) instead of delivering it. So the viewport holds focus
    // whenever nothing else (a text field) does.
    private val keepFocus = android.view.ViewTreeObserver.OnGlobalFocusChangeListener { _, newFocus ->
        if (newFocus == null) post { if (isAttachedToWindow && rootView.findFocus() == null) requestFocus() }
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        session.renderTarget = this
        com.higglerslab.sculpt3d.ui.ViewportFocus.request = { requestFocus() }
        viewTreeObserver.addOnGlobalFocusChangeListener(keepFocus)
        post { if (rootView.findFocus() == null) requestFocus() }
    }

    override fun onDetachedFromWindow() {
        viewTreeObserver.removeOnGlobalFocusChangeListener(keepFocus)
        if (session.renderTarget === this) session.renderTarget = null
        surfaceReady = false
        super.onDetachedFromWindow()
    }

    /**
     * A PNG of the view at `scale` × its size in points (Screenshot 2×, Visualize ▸ Capture 4×),
     * rendered on the GL thread; `done` gets it (or null) on the main thread.
     */
    fun snapshotPng(scale: Float, done: (ByteArray?) -> Unit) {
        val w = (width / density * scale).toInt().coerceIn(1, 8192)
        val h = (height / density * scale).toInt().coerceIn(1, 8192)
        queueEvent {
            val png = try { SculptNative.viewSnapshotPng(doc, w, h) } catch (t: Throwable) { null }
            post { done(png) }
        }
    }

    private inner class SculptRenderer : Renderer {
        private var lastNeeds = false

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            Log.i(TAG, "GL surface created: ${GLES30.glGetString(GLES30.GL_VERSION)} / ${GLES30.glGetString(GLES30.GL_RENDERER)}")
            SculptNative.viewGlInit(doc)
            surfaceReady = true
            post { session.refresh(force = true) }
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            GLES30.glViewport(0, 0, width, height)
            SculptNative.viewResize(doc, width, height, density)
            post { session.refreshAnnotations(force = true) }
        }

        override fun onDrawFrame(gl: GL10?) {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            SculptNative.viewRender(doc)
            val err = GLES30.glGetError()
            if (err != GLES30.GL_NO_ERROR) Log.w(TAG, "GL error after render: 0x${Integer.toHexString(err)}")
            // Keep drawing while the core asks for it (queued MCP screenshots, transitions).
            if (SculptNative.viewNeedsRedraw(doc)) requestRender()
            post { session.refreshAnnotations() }
        }
    }

    /** RGBA8888, depth 24, stencil 8 with 4× MSAA; falls back to no MSAA, then to depth 16. */
    private class MsaaConfigChooser : EGLConfigChooser {
        override fun chooseConfig(egl: EGL10, display: EGLDisplay): EGLConfig {
            val es3 = 0x40 // EGL_OPENGL_ES3_BIT_KHR
            val attempts = listOf(
                intArrayOf(EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_ALPHA_SIZE, 8,
                    EGL10.EGL_DEPTH_SIZE, 24, EGL10.EGL_STENCIL_SIZE, 8, EGL10.EGL_RENDERABLE_TYPE, es3,
                    EGL10.EGL_SAMPLE_BUFFERS, 1, EGL10.EGL_SAMPLES, 4, EGL10.EGL_NONE),
                intArrayOf(EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_ALPHA_SIZE, 8,
                    EGL10.EGL_DEPTH_SIZE, 24, EGL10.EGL_STENCIL_SIZE, 8, EGL10.EGL_RENDERABLE_TYPE, es3, EGL10.EGL_NONE),
                intArrayOf(EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8,
                    EGL10.EGL_DEPTH_SIZE, 16, EGL10.EGL_RENDERABLE_TYPE, es3, EGL10.EGL_NONE),
            )
            for ((i, attrs) in attempts.withIndex()) {
                val num = IntArray(1)
                if (!egl.eglChooseConfig(display, attrs, null, 0, num) || num[0] <= 0) continue
                val configs = arrayOfNulls<EGLConfig>(num[0])
                egl.eglChooseConfig(display, attrs, configs, num[0], num)
                // Prefer an exact 8-bit colour match.
                val v = IntArray(1)
                val pick = configs.filterNotNull().firstOrNull { c ->
                    egl.eglGetConfigAttrib(display, c, EGL10.EGL_RED_SIZE, v) && v[0] == 8 &&
                        egl.eglGetConfigAttrib(display, c, EGL10.EGL_GREEN_SIZE, v) && v[0] == 8
                } ?: configs.firstOrNull()
                if (pick != null) {
                    Log.i(TAG, "EGL config: attempt ${i + 1} (${if (i == 0) "4x MSAA" else "no MSAA"})")
                    return pick
                }
            }
            throw IllegalArgumentException("No OpenGL ES 3 config")
        }
    }

    // ---- Input ----

    private var twoFinger = false
    private var lastCx = 0f
    private var lastCy = 0f
    private var lastSpan = 0f
    private var lastAngle = 0f
    private var twist = 0f
    private var twisting = false

    private fun mods(e: MotionEvent): Int = modsFromMeta(e.metaState)

    private fun pointerType(e: MotionEvent, i: Int): Int = when (e.getToolType(i)) {
        MotionEvent.TOOL_TYPE_MOUSE -> SculptNative.POINTER_MOUSE
        MotionEvent.TOOL_TYPE_STYLUS, MotionEvent.TOOL_TYPE_ERASER -> SculptNative.POINTER_PEN
        else -> if (e.isFromSource(InputDevice.SOURCE_MOUSE)) SculptNative.POINTER_MOUSE else SculptNative.POINTER_TOUCH
    }

    private fun button(e: MotionEvent): Int {
        val b = e.buttonState
        return when {
            b and MotionEvent.BUTTON_SECONDARY != 0 || b and MotionEvent.BUTTON_STYLUS_PRIMARY != 0 -> SculptNative.BUTTON_SECONDARY
            b and MotionEvent.BUTTON_TERTIARY != 0 -> SculptNative.BUTTON_MIDDLE
            else -> SculptNative.BUTTON_PRIMARY
        }
    }

    private fun send(id: Int, type: Int, button: Int, phase: Int, xPx: Float, yPx: Float, mods: Int) {
        val x = xPx / density
        val y = yPx / density
        session.onGl { SculptNative.viewPointer(doc, id, type, button, phase, x, y, mods) }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (e.actionMasked == MotionEvent.ACTION_DOWN) {
            requestFocus()
            onTouchDown?.invoke()
        }
        val m = mods(e)
        val btn = button(e)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                send(e.getPointerId(i), pointerType(e, i), btn, SculptNative.PHASE_DOWN, e.getX(i), e.getY(i), m)
                if (e.pointerCount >= 2) beginTwoFinger(e)
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    send(e.getPointerId(i), pointerType(e, i), btn, SculptNative.PHASE_MOVE, e.getX(i), e.getY(i), m)
                }
                if (twoFinger && e.pointerCount >= 2) moveTwoFinger(e)
            }
            MotionEvent.ACTION_POINTER_UP -> {
                val i = e.actionIndex
                send(e.getPointerId(i), pointerType(e, i), btn, SculptNative.PHASE_UP, e.getX(i), e.getY(i), m)
                // Re-baseline on the remaining fingers.
                if (e.pointerCount - 1 >= 2) beginTwoFinger(e, skip = i) else twoFinger = false
            }
            MotionEvent.ACTION_UP -> {
                send(e.getPointerId(0), pointerType(e, 0), btn, SculptNative.PHASE_UP, e.x, e.y, m)
                twoFinger = false
            }
            MotionEvent.ACTION_CANCEL -> {
                for (i in 0 until e.pointerCount) {
                    send(e.getPointerId(i), pointerType(e, i), btn, SculptNative.PHASE_CANCEL, e.getX(i), e.getY(i), m)
                }
                twoFinger = false
            }
        }
        return true
    }

    private fun twoPointers(e: MotionEvent, skip: Int): Pair<Int, Int> {
        val idx = (0 until e.pointerCount).filter { it != skip }
        return idx[0] to idx[1]
    }

    private fun beginTwoFinger(e: MotionEvent, skip: Int = -1) {
        val (a, b) = twoPointers(e, skip)
        lastCx = (e.getX(a) + e.getX(b)) / 2
        lastCy = (e.getY(a) + e.getY(b)) / 2
        lastSpan = hypot(e.getX(a) - e.getX(b), e.getY(a) - e.getY(b))
        lastAngle = atan2(e.getY(b) - e.getY(a), e.getX(b) - e.getX(a))
        twist = 0f
        twisting = false
        twoFinger = true
    }

    private fun moveTwoFinger(e: MotionEvent) {
        val (a, b) = twoPointers(e, -1)
        val cx = (e.getX(a) + e.getX(b)) / 2
        val cy = (e.getY(a) + e.getY(b)) / 2
        val span = hypot(e.getX(a) - e.getX(b), e.getY(a) - e.getY(b))
        val angle = atan2(e.getY(b) - e.getY(a), e.getX(b) - e.getX(a))
        val dx = (cx - lastCx) / density
        val dy = (cy - lastCy) / density
        val factor = if (lastSpan > 1f && span > 1f) span / lastSpan else 1f
        var dAngle = angle - lastAngle
        if (dAngle > Math.PI) dAngle -= (2 * Math.PI).toFloat()
        if (dAngle < -Math.PI) dAngle += (2 * Math.PI).toFloat()
        lastCx = cx; lastCy = cy; lastSpan = span; lastAngle = angle
        val px = cx / density
        val py = cy / density
        // Twist turns the model about the vertical axis once it is clearly a twist (> 12°).
        twist += dAngle
        if (!twisting && kotlin.math.abs(twist) > Math.toRadians(12.0)) twisting = true
        val orbitDx = if (twisting) Math.toDegrees(dAngle.toDouble()).toFloat() * 2.5f else 0f
        session.onGl {
            if (dx != 0f || dy != 0f) SculptNative.viewPan(doc, dx, dy)
            if (factor != 1f) SculptNative.viewZoom(doc, factor, px, py)
            if (orbitDx != 0f) SculptNative.viewOrbit(doc, orbitDx, 0f)
        }
    }

    override fun onHoverEvent(e: MotionEvent): Boolean {
        val type = pointerType(e, 0)
        when (e.actionMasked) {
            MotionEvent.ACTION_HOVER_ENTER, MotionEvent.ACTION_HOVER_MOVE ->
                send(e.getPointerId(0), type, SculptNative.BUTTON_PRIMARY, SculptNative.PHASE_HOVER, e.x, e.y, mods(e))
            MotionEvent.ACTION_HOVER_EXIT ->
                send(e.getPointerId(0), type, SculptNative.BUTTON_PRIMARY, SculptNative.PHASE_EXIT, e.x, e.y, mods(e))
        }
        return true
    }

    override fun onGenericMotionEvent(e: MotionEvent): Boolean {
        if (e.isFromSource(InputDevice.SOURCE_CLASS_POINTER) && e.actionMasked == MotionEvent.ACTION_SCROLL) {
            val v = e.getAxisValue(MotionEvent.AXIS_VSCROLL)
            val h = e.getAxisValue(MotionEvent.AXIS_HSCROLL)
            val x = e.x / density
            val y = e.y / density
            val shift = e.metaState and KeyEvent.META_SHIFT_ON != 0
            session.onGl {
                when {
                    shift -> SculptNative.viewPan(doc, -h * 20f, v * 20f)
                    v != 0f -> SculptNative.viewZoom(doc, 1.1f.pow(v), x, y)
                    h != 0f -> SculptNative.viewOrbit(doc, -h * 20f, 0f)
                }
            }
            return true
        }
        return super.onGenericMotionEvent(e)
    }

    companion object {
        fun modsFromMeta(meta: Int): Int {
            var m = 0
            if (meta and KeyEvent.META_SHIFT_ON != 0) m = m or SculptNative.MOD_SHIFT
            if (meta and (KeyEvent.META_CTRL_ON or KeyEvent.META_META_ON) != 0) m = m or SculptNative.MOD_CONTROL
            if (meta and KeyEvent.META_ALT_ON != 0) m = m or SculptNative.MOD_ALT
            return m
        }

        /** The core's key name for a hardware key, or null. */
        fun keyName(e: KeyEvent): String? = when (e.keyCode) {
            KeyEvent.KEYCODE_ESCAPE -> "Escape"
            KeyEvent.KEYCODE_DEL -> "Backspace"
            KeyEvent.KEYCODE_FORWARD_DEL -> "Delete"
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> "Enter"
            KeyEvent.KEYCODE_TAB -> "Tab"
            KeyEvent.KEYCODE_DPAD_UP -> "Up"
            KeyEvent.KEYCODE_DPAD_DOWN -> "Down"
            KeyEvent.KEYCODE_DPAD_LEFT -> "Left"
            KeyEvent.KEYCODE_DPAD_RIGHT -> "Right"
            in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> ('a' + (e.keyCode - KeyEvent.KEYCODE_A)).toString()
            in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> ('0' + (e.keyCode - KeyEvent.KEYCODE_0)).toString()
            else -> null
        }
    }
}
