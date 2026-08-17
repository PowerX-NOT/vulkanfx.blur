package com.vulkanfx.blur

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.view.PixelCopy
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup

/**
 * Snapshot of layers behind a blur host, matching AOSP RenderEngine's
 * `makeTemporaryImage()` of already-composited content below a blur layer.
 *
 * Window views are redrawn (SurfaceView hole-punch would make PixelCopy of the
 * window empty in the host rect). Sibling [SurfaceView]s are PixelCopied —
 * each copy is a SurfaceFlinger layer snapshot, composited in z-order.
 */
internal class SceneCapture {
    private val handler = Handler(Looper.getMainLooper())
    private val hostLoc = IntArray(2)
    private val layerLoc = IntArray(2)
    private val surfaceViews = ArrayList<SurfaceView>()
    private var dest: Bitmap? = null
    private var copyInFlight = false

    fun snapshot(host: View): Bitmap? {
        if (copyInFlight) return null
        if (!host.isAttachedToWindow || host.width <= 0 || host.height <= 0) return null
        val (w, h) = VulkanBlur.captureSize(host.width, host.height)
        val bmp = ensureDest(w, h)
        bmp.eraseColor(Color.TRANSPARENT)
        val canvas = Canvas(bmp)
        host.getLocationInWindow(hostLoc)
        canvas.scale(w / host.width.toFloat(), h / host.height.toFloat())
        canvas.translate(-hostLoc[0].toFloat(), -hostLoc[1].toFloat())
        surfaceViews.clear()
        drawSkipping(host.rootView, host, canvas)
        return bmp
    }

    /**
     * PixelCopy each SurfaceView behind [host] onto the last [snapshot] bitmap.
     * Returns immediately; [onDone] gets the composited bitmap or null if nothing to copy.
     */
    fun snapshotSurfaceLayers(host: View, onDone: (Bitmap?) -> Unit) {
        val layers = surfaceViews.filter { it !== host && it.holder.surface?.isValid == true }
        surfaceViews.clear()
        val bmp = dest
        if (layers.isEmpty() || bmp == null || copyInFlight) {
            onDone(null)
            return
        }
        copyInFlight = true
        var remaining = layers.size
        val scaleX = bmp.width / host.width.toFloat()
        val scaleY = bmp.height / host.height.toFloat()
        host.getLocationInWindow(hostLoc)
        for (sv in layers) {
            val lw = (sv.width * scaleX).toInt().coerceAtLeast(1)
            val lh = (sv.height * scaleY).toInt().coerceAtLeast(1)
            val tmp = Bitmap.createBitmap(lw, lh, Bitmap.Config.ARGB_8888)
            val surface = sv.holder.surface
            if (surface == null || !surface.isValid) {
                tmp.recycle()
                if (--remaining == 0) {
                    copyInFlight = false
                    onDone(bmp)
                }
                continue
            }
            try {
                PixelCopy.request(surface, tmp, { result ->
                    if (result == PixelCopy.SUCCESS) {
                        sv.getLocationInWindow(layerLoc)
                        Canvas(bmp).drawBitmap(
                            tmp,
                            (layerLoc[0] - hostLoc[0]) * scaleX,
                            (layerLoc[1] - hostLoc[1]) * scaleY,
                            null,
                        )
                    }
                    tmp.recycle()
                    if (--remaining == 0) {
                        copyInFlight = false
                        onDone(bmp)
                    }
                }, handler)
            } catch (_: IllegalArgumentException) {
                tmp.recycle()
                if (--remaining == 0) {
                    copyInFlight = false
                    onDone(bmp)
                }
            }
        }
    }

    fun release() {
        dest?.recycle()
        dest = null
        surfaceViews.clear()
        copyInFlight = false
    }

    private fun ensureDest(w: Int, h: Int): Bitmap {
        val cur = dest
        if (cur != null && cur.width == w && cur.height == h && !cur.isRecycled) return cur
        cur?.recycle()
        return Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888).also { dest = it }
    }

    private fun drawSkipping(view: View, skip: View, canvas: Canvas) {
        if (view === skip || view.visibility != View.VISIBLE) return
        if (view is SurfaceView) {
            surfaceViews.add(view)
            return
        }
        if (view is ViewGroup) {
            if (!containsView(view, skip)) {
                if (drawnOnTopOf(view, skip)) return
                view.draw(canvas)
                collectSurfaceViews(view, skip)
                return
            }
            view.background?.draw(canvas)
            for (i in 0 until view.childCount) {
                val child = view.getChildAt(i)
                if (drawnOnTopOf(child, skip) && !containsView(child, skip)) continue
                canvas.save()
                canvas.translate(
                    (child.left - view.scrollX).toFloat(),
                    (child.top - view.scrollY).toFloat(),
                )
                if (!child.matrix.isIdentity) canvas.concat(child.matrix)
                drawSkipping(child, skip, canvas)
                canvas.restore()
            }
            return
        }
        if (!drawnOnTopOf(view, skip)) view.draw(canvas)
    }

    private fun collectSurfaceViews(view: View, skip: View) {
        if (view === skip || view.visibility != View.VISIBLE) return
        if (view is SurfaceView) {
            surfaceViews.add(view)
            return
        }
        if (view is ViewGroup) {
            for (i in 0 until view.childCount) collectSurfaceViews(view.getChildAt(i), skip)
        }
    }

    private fun containsView(parent: View, child: View): Boolean {
        var v: View? = child
        while (v != null) {
            if (v === parent) return true
            v = v.parent as? View
        }
        return false
    }

    // ponytail: sibling index as z-order; custom drawing-order / elevation would need getChildDrawingOrder
    private fun drawnOnTopOf(view: View, host: View): Boolean {
        if (view === host || containsView(view, host)) return false
        val viewChain = ancestors(view)
        val hostChain = ancestors(host)
        var i = 0
        val n = minOf(viewChain.size, hostChain.size)
        while (i < n && viewChain[i] === hostChain[i]) i++
        if (i == 0) return false
        val common = viewChain[i - 1] as? ViewGroup ?: return false
        val viewChild = if (i < viewChain.size) viewChain[i] else view
        val hostChild = if (i < hostChain.size) hostChain[i] else host
        return common.indexOfChild(viewChild) > common.indexOfChild(hostChild)
    }

    /** Root-first ancestors including [view]. */
    private fun ancestors(view: View): List<View> {
        val out = ArrayList<View>()
        var v: View? = view
        while (v != null) {
            out.add(v)
            v = v.parent as? View
        }
        out.reverse()
        return out
    }
}
