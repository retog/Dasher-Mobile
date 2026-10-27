package com.janmurin.dashermobile

import android.text.InputType
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import java.lang.reflect.Proxy
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DasherImeTextSinkTest {

    /** An editor holding only the text before its cursor, edited through an [InputConnection]. */
    private class FakeEditor(inputType: Int, readable: Boolean = false) {
        val text = StringBuilder()
        var reads = 0
        val editorInfo = EditorInfo().apply { this.inputType = inputType }
        val connection = Proxy.newProxyInstance(
            InputConnection::class.java.classLoader,
            arrayOf(InputConnection::class.java)
        ) { _, method, args ->
            when (method.name) {
                "beginBatchEdit", "endBatchEdit" -> true
                "commitText" -> {
                    text.append(args[0] as CharSequence)
                    true
                }
                "deleteSurroundingText" -> {
                    text.setLength(maxOf(0, text.length - args[0] as Int))
                    true
                }
                "getTextBeforeCursor" -> {
                    reads++
                    text.takeLast(args[0] as Int)
                }
                else -> throw UnsupportedOperationException(method.name)
            }
        } as InputConnection
        val sink = DasherImeTextSink({ connection }, { editorInfo }, { readable })
    }

    @Test
    fun multiLineEditorTakesNewlines() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE)
        editor.sink.onTextChanged("ab\ncd")
        assertEquals("ab\ncd", editor.text.toString())
        editor.sink.onTextChanged("ab")
        assertEquals("ab", editor.text.toString())
    }

    @Test
    fun singleLineEditorDropsNewlinesAndNeverDeletesForThem() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT)
        editor.text.append("x")
        editor.sink.onTextChanged("ab\ncd")
        assertEquals("xabcd", editor.text.toString())
        editor.sink.onTextChanged("ab")
        assertEquals("xab", editor.text.toString())
        editor.sink.onTextChanged("")
        assertEquals("x", editor.text.toString())
    }

    @Test
    fun imeMultiLineFlagStillMeansSingleLine() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_IME_MULTI_LINE)
        editor.sink.onTextChanged("a\nb")
        assertEquals("ab", editor.text.toString())
    }

    @Test
    fun reversingIntoTheBaselineDeletesTheEditorsText() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT)
        editor.text.append("ahoj svet")
        editor.sink.resetTracking("svet")
        editor.sink.onTextChanged("sv")
        assertEquals("ahoj sv", editor.text.toString())
        editor.sink.onTextChanged("svaly")
        assertEquals("ahoj svaly", editor.text.toString())
    }

    @Test
    fun committedTextLeavesOutNewlinesTheEditorNeverGot() {
        val single = FakeEditor(InputType.TYPE_CLASS_TEXT)
        single.sink.onTextChanged("ab\ncd")
        assertEquals("abcd", single.sink.committedText)
        assertEquals(single.text.toString(), single.sink.committedText)

        val multi = FakeEditor(InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE)
        multi.sink.onTextChanged("ab\ncd")
        assertEquals("ab\ncd", multi.sink.committedText)
        assertTrue(multi.sink.acceptsNewlines)
        assertFalse(single.sink.acceptsNewlines)
    }

    @Test
    fun reportsWhatItDid() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT)
        assertEquals(DasherImeTextSink.Result.EDITED, editor.sink.onTextChanged("a"))
        assertEquals(DasherImeTextSink.Result.UNCHANGED, editor.sink.onTextChanged("a"))
        assertEquals(DasherImeTextSink.Result.UNCHANGED, DasherImeTextSink({ null }, { null }).onTextChanged("a"))
    }

    @Test
    fun deletesNothingTheEditorDoesNotHoldBeforeItsCursor() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT, readable = true)
        editor.text.append("ahoj svet")
        editor.sink.resetTracking("svet")
        // The user moved the cursor: the editor now holds other text before it.
        editor.text.setLength(4)
        assertEquals(DasherImeTextSink.Result.OUT_OF_SYNC, editor.sink.onTextChanged("sve"))
        assertEquals("ahoj", editor.text.toString())
        assertEquals("svet", editor.sink.committedText)
    }

    @Test
    fun takesANonBreakingSpaceForTheSpaceItTyped() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT, readable = true)
        editor.text.append("a\u00A0")
        editor.sink.resetTracking("a ")
        assertEquals(DasherImeTextSink.Result.EDITED, editor.sink.onTextChanged("a"))
        assertEquals("a", editor.text.toString())
    }

    @Test
    fun neverReadsAnEditorThatMustNotBeRead() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT, readable = false)
        editor.sink.onTextChanged("abc")
        editor.sink.onTextChanged("a")
        assertEquals("a", editor.text.toString())
        assertEquals(0, editor.reads)
    }
}
