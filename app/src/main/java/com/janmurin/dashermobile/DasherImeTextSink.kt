package com.janmurin.dashermobile

import android.text.InputType
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection

/**
 * Bridges the DasherCore text output to an active [InputConnection].
 *
 * Instead of replacing the entire field text on every change, [DasherImeTextSink] computes the
 * **diff** between the previous output and the new output and sends only the minimal set of
 * [InputConnection.deleteSurroundingText] and [InputConnection.commitText] calls required to
 * bring the target field in sync.  This preserves the host app's undo history and is
 * significantly cheaper than re-inserting the full string each frame.
 *
 * ## Newlines
 * A newline is committed only in multi-line editors and dropped in single-line ones.  The sink
 * never performs the editor's IME action ("Go", "Search", "Send"): Dasher outputs a symbol as soon
 * as the user steers into its box, so a brush with the ¶ box would send a half-written message.
 * (Chrome and WebView still turn a newline committed on its own into an Enter key press, which a
 * web page may take as Send.)  Dropped newlines are left out when counting what to delete, so
 * reversing over one never deletes a character of the editor's text.
 *
 * @param inputConnectionProvider Lazy getter for the current [InputConnection].  May return
 *   `null` if the connection is no longer valid.
 * @param editorInfoProvider       Lazy getter for the current [EditorInfo].  Used to tell
 *   whether the editor takes newlines.
 */
class DasherImeTextSink(
    private val inputConnectionProvider: () -> InputConnection?,
    private val editorInfoProvider: () -> EditorInfo?
) {
    private var lastText: String = ""

    /**
     * Resets the internal "last known text" snapshot.
     *
     * Should be called whenever the IME attaches to a new editor field so that the next
     * [onTextChanged] call computes its diff against [baseline].
     *
     * @param baseline Text already before the cursor that the engine was loaded with.
     */
    fun resetTracking(baseline: String = "") {
        lastText = baseline
    }

    /**
     * The text the editor is believed to hold before its cursor: the output text without the
     * newlines a single-line editor never received.
     */
    val committedText: String
        get() = if (acceptsNewlines) lastText else lastText.replace("\n", "")

    /**
     * `true` if the current editor takes newlines.  As in `TextView`, only a text editor with
     * [InputType.TYPE_TEXT_FLAG_MULTI_LINE] does; [InputType.TYPE_TEXT_FLAG_IME_MULTI_LINE] marks
     * a single-line editor.
     */
    val acceptsNewlines: Boolean
        get() {
            val inputType = editorInfoProvider()?.inputType ?: 0
            val mask = InputType.TYPE_MASK_CLASS or InputType.TYPE_TEXT_FLAG_MULTI_LINE
            return (inputType and mask) == (InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE)
        }

    /**
     * Called each time DasherCore's output buffer changes.
     *
     * Computes the diff between [text] and the previously seen output, then commits the
     * minimum required edits to the active [InputConnection].  No-op when [text] equals the
     * last seen value.
     *
     * @param text The full output text produced by DasherCore so far in this session.
     * @return `false` if [text] is the last seen value or there is no editor to edit.
     */
    fun onTextChanged(text: String): Boolean {
        if (text == lastText) {
            return false
        }

        val inputConnection = inputConnectionProvider() ?: return false
        val commonPrefixLength = commonPrefixLength(lastText, text)
        val deleteCount = committedLength(lastText.substring(commonPrefixLength))
        val insertedText = text.substring(commonPrefixLength)

        inputConnection.beginBatchEdit()
        if (deleteCount > 0) {
            inputConnection.deleteSurroundingText(deleteCount, 0)
        }
        if (insertedText.isNotEmpty()) {
            commitInsertedText(inputConnection, insertedText)
        }
        inputConnection.endBatchEdit()
        lastText = text
        return true
    }

    private fun commitInsertedText(inputConnection: InputConnection, insertedText: String) {
        val chunk = StringBuilder()

        fun flushChunk() {
            if (chunk.isNotEmpty()) {
                inputConnection.commitText(chunk.toString(), 1)
                chunk.setLength(0)
            }
        }

        insertedText.forEach { ch ->
            when (ch) {
                ' ' -> {
                    flushChunk()
                    inputConnection.commitText(" ", 1)
                }
                '\n' -> {
                    flushChunk()
                    if (acceptsNewlines) {
                        inputConnection.commitText("\n", 1)
                    }
                }
                else -> chunk.append(ch)
            }
        }

        flushChunk()
    }

    /** Returns how many characters of [text] were committed, i.e. without dropped newlines. */
    private fun committedLength(text: String): Int =
        if (acceptsNewlines) text.length else text.count { it != '\n' }

    private fun commonPrefixLength(left: String, right: String): Int {
        val max = minOf(left.length, right.length)
        var index = 0
        while (index < max && left[index] == right[index]) {
            index++
        }
        return index
    }
}
