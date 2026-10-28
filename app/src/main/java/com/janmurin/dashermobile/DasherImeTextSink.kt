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
 * ## Deleting
 * Where the editor's text may be read, the sink first checks that the text before the cursor ends
 * with what it is about to delete and up to [CHECKED_CONTEXT] characters before that, as it
 * believes them: its own text, and before it the editor text read when the engine was loaded.
 * If it does not (the user moved the cursor, or the editor dropped a commit, e.g. at its length
 * limit), it deletes nothing and reports [Result.OUT_OF_SYNC].  Case and the characters
 * [asTyped] maps are ignored, as editors rewrite them without moving the cursor.
 *
 * @param inputConnectionProvider Lazy getter for the current [InputConnection].  May return
 *   `null` if the connection is no longer valid.
 * @param editorInfoProvider       Lazy getter for the current [EditorInfo].  Used to tell
 *   whether the editor takes newlines.
 * @param mayReadText             Returns `true` if the editor's text may be read to check what
 *   is deleted: not in password fields, nor in editors that hold no text (terminals).
 */
class DasherImeTextSink(
    private val inputConnectionProvider: () -> InputConnection?,
    private val editorInfoProvider: () -> EditorInfo?,
    private val mayReadText: () -> Boolean = { false }
) {
    companion object {
        /** Characters before the deleted ones that must match too, so a chance match is unlikely. */
        const val CHECKED_CONTEXT = 24

        /**
         * Returns editor text as Dasher would have typed it: web editors hold a space typed last
         * as a non-breaking one, and some editors turn quotes into typographic ones.
         */
        fun asTyped(text: String): String = text
            .replace('\u00A0', ' ')
            .replace('\u2018', '\'')
            .replace('\u2019', '\'')
            .replace('\u201C', '"')
            .replace('\u201D', '"')
    }

    /** What [onTextChanged] did. */
    enum class Result {
        /** Nothing: the text is the last seen one, or there is no editor. */
        UNCHANGED,
        /** The editor was edited. */
        EDITED,
        /** Nothing: the editor's text before its cursor is not the text Dasher would delete. */
        OUT_OF_SYNC
    }

    private var lastText: String = ""
    /** Editor text before [lastText], read but not loaded into the engine; checked when deleting. */
    private var textBefore: String = ""

    /**
     * Resets the internal "last known text" snapshot.
     *
     * Should be called whenever the IME attaches to a new editor field so that the next
     * [onTextChanged] call computes its diff against [baseline].
     *
     * @param baseline   Text already before the cursor that the engine was loaded with.
     * @param textBefore Editor text before [baseline] that was read but not loaded.
     */
    fun resetTracking(baseline: String = "", textBefore: String = "") {
        lastText = baseline
        this.textBefore = textBefore
    }

    /**
     * The text the editor is believed to hold before its cursor: the output text without the
     * newlines a single-line editor never received.
     */
    val committedText: String
        get() = committed(lastText)

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
     */
    fun onTextChanged(text: String): Result {
        if (text == lastText) {
            return Result.UNCHANGED
        }

        val inputConnection = inputConnectionProvider() ?: return Result.UNCHANGED
        val commonPrefixLength = commonPrefixLength(lastText, text)
        val deletedText = committed(lastText.substring(commonPrefixLength))
        val insertedText = text.substring(commonPrefixLength)
        if (deletedText.isNotEmpty() && mayReadText()) {
            val expected = (textBefore + committed(lastText)).takeLast(deletedText.length + CHECKED_CONTEXT)
            if (!endsWith(inputConnection, expected)) {
                return Result.OUT_OF_SYNC
            }
        }

        inputConnection.beginBatchEdit()
        if (deletedText.isNotEmpty()) {
            inputConnection.deleteSurroundingText(deletedText.length, 0)
        }
        if (insertedText.isNotEmpty()) {
            commitInsertedText(inputConnection, insertedText)
        }
        inputConnection.endBatchEdit()
        lastText = text
        return Result.EDITED
    }

    /** Returns `true` if the editor's text before its cursor ends with [expected]. */
    private fun endsWith(inputConnection: InputConnection, expected: String): Boolean {
        val actual = inputConnection.getTextBeforeCursor(expected.length, 0)?.toString() ?: return false
        return asTyped(actual).equals(asTyped(expected), ignoreCase = true)
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

    /** Returns what of [text] the editor received, i.e. [text] without dropped newlines. */
    private fun committed(text: String): String =
        if (acceptsNewlines) text else text.replace("\n", "")

    private fun commonPrefixLength(left: String, right: String): Int {
        val max = minOf(left.length, right.length)
        var index = 0
        while (index < max && left[index] == right[index]) {
            index++
        }
        return index
    }
}
