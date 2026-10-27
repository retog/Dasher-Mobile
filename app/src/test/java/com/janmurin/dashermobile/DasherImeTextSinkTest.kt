package com.janmurin.dashermobile

import android.text.InputType
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import java.lang.reflect.Proxy
import org.junit.Assert.assertEquals
import org.junit.Test

class DasherImeTextSinkTest {

    /** An editor holding only the text before its cursor, edited through an [InputConnection]. */
    private class FakeEditor(inputType: Int) {
        val text = StringBuilder()
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
                else -> throw UnsupportedOperationException(method.name)
            }
        } as InputConnection
        val sink = DasherImeTextSink({ connection }, { editorInfo })
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
}
