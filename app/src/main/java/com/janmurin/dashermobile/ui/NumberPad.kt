package com.janmurin.dashermobile.ui

import android.content.Context
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import com.janmurin.dashermobile.R

/**
 * A pad of large buttons for entering numbers, shown over the Dasher canvas in place of it.
 *
 * Dasher's alphabet has no digits: a language model predicts little about them, so ten boxes of
 * much the same size would be slower to steer into than ten buttons are to tap.
 *
 * ```
 *  1   2   3   ⌫
 *  4   5   6   .
 *  7   8   9   ,
 * abc    0     ␣
 * ```
 *
 * The pad only reports the buttons tapped; the host enters the text.
 */
class NumberPad(context: Context) : LinearLayout(context) {

    private companion object {
        const val BACKSPACE = "⌫"
        const val SPACE = "␣"
        /** Holding backspace deletes again after this long, then at every interval. */
        const val REPEAT_DELAY_MS = 400L
        const val REPEAT_INTERVAL_MS = 70L
    }

    /** Called with the text of a tapped digit, separator or space button. */
    var onText: (String) -> Unit = {}
    /** Called when backspace is tapped, and repeatedly while it is held. */
    var onBackspace: () -> Unit = {}
    /** Called when the button that leads back to Dasher is tapped. */
    var onClose: () -> Unit = {}

    private val repeatBackspace = object : Runnable {
        override fun run() {
            onBackspace()
            postDelayed(this, REPEAT_INTERVAL_MS)
        }
    }

    init {
        orientation = VERTICAL
        setBackgroundColor(0xFF3A3A3A.toInt())
        // Touches between the buttons must not reach the canvas below.
        isClickable = true
        val gap = dp(3)
        setPadding(gap, gap, gap, gap)

        addRow(key("1"), key("2"), key("3"), backspaceKey())
        addRow(key("4"), key("5"), key("6"), key("."))
        addRow(key("7"), key("8"), key("9"), key(","))
        addRow(
            closeKey(),
            key("0", weight = 2f),
            key(SPACE, text = " ", description = context.getString(R.string.number_pad_space))
        )
    }

    override fun onVisibilityChanged(changedView: View, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        if (visibility != VISIBLE) removeCallbacks(repeatBackspace)
    }

    override fun onDetachedFromWindow() {
        removeCallbacks(repeatBackspace)
        super.onDetachedFromWindow()
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private fun addRow(vararg keys: View) {
        val row = LinearLayout(context).apply { orientation = HORIZONTAL }
        keys.forEach(row::addView)
        addView(row, LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
    }

    private fun button(label: String, weight: Float, description: String?): TextView =
        TextView(context).apply {
            text = label
            textSize = 28f
            gravity = Gravity.CENTER
            setTextColor(0xFFFFFFFF.toInt())
            setBackgroundResource(R.drawable.button_background)
            contentDescription = description
            isClickable = true
            isFocusable = true
            layoutParams = LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, weight).apply {
                val gap = dp(3)
                setMargins(gap, gap, gap, gap)
            }
        }

    private fun key(
        label: String,
        text: String = label,
        weight: Float = 1f,
        description: String? = null
    ): TextView = button(label, weight, description).apply {
        setOnClickListener { onText(text) }
    }

    private fun closeKey(): TextView =
        button("abc", 1f, context.getString(R.string.number_pad_close)).apply {
            textSize = 22f
            setOnClickListener { onClose() }
        }

    private fun backspaceKey(): TextView =
        button(BACKSPACE, 1f, context.getString(R.string.icon_delete)).apply {
            // For accessibility services, which click without touching.
            setOnClickListener { onBackspace() }
            setOnTouchListener { view, event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        view.isPressed = true
                        onBackspace()
                        this@NumberPad.postDelayed(repeatBackspace, REPEAT_DELAY_MS)
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        view.isPressed = false
                        this@NumberPad.removeCallbacks(repeatBackspace)
                    }
                }
                true
            }
        }
}
