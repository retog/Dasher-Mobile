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
        /** Characters beyond this length are dropped, like a length-limited field's. */
        var maxLength = Int.MAX_VALUE
        var allCaps = false
        val editorInfo = EditorInfo().apply { this.inputType = inputType }
        val connection = Proxy.newProxyInstance(
            InputConnection::class.java.classLoader,
            arrayOf(InputConnection::class.java)
        ) { _, method, args ->
            when (method.name) {
                "beginBatchEdit", "endBatchEdit" -> true
                "commitText" -> {
                    val committed = (args[0] as CharSequence).toString()
                    text.append(if (allCaps) committed.uppercase() else committed)
                    if (text.length > maxLength) text.setLength(maxLength)
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
    fun deletesNothingWhenOnlyTheDeletedCharacterHappensToMatch() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT, readable = true)
        editor.text.append("hello")
        editor.maxLength = 5
        editor.sink.resetTracking("hello")
        editor.sink.onTextChanged("helloo")
        assertEquals("hello", editor.text.toString())
        // Reversing the dropped "o" must not delete the user's "o".
        assertEquals(DasherImeTextSink.Result.OUT_OF_SYNC, editor.sink.onTextChanged("hello"))
        assertEquals("hello", editor.text.toString())
    }

    @Test
    fun checksTheEditorTextReadBeforeTheLoadedText() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT, readable = true)
        editor.text.append("Room 7 is free, room 9 is free")
        // Loading was cut at the digit; the text before it was read but not loaded.
        editor.sink.resetTracking(" is free", textBefore = "Room 7 is free, room 9")
        // The user moved the cursor after "Room 7 is free".
        editor.text.setLength(14)
        assertEquals(DasherImeTextSink.Result.OUT_OF_SYNC, editor.sink.onTextChanged(" is fre"))
        assertEquals("Room 7 is free", editor.text.toString())
    }

    @Test
    fun takesTypographicQuotesForTheQuotesItTyped() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT, readable = true)
        editor.text.append("say \u201Chi\u201D, \u201Eahoj\u201C")
        editor.sink.resetTracking("say \"hi\", \"ahoj\"")
        assertEquals(DasherImeTextSink.Result.EDITED, editor.sink.onTextChanged("say \"hi\", \"aho"))
        assertEquals("say \u201Chi\u201D, \u201Eaho", editor.text.toString())
    }

    @Test
    fun reversesInAnEditorThatCapitalises() {
        val editor = FakeEditor(InputType.TYPE_CLASS_TEXT, readable = true)
        editor.allCaps = true
        editor.sink.onTextChanged("ab")
        assertEquals("AB", editor.text.toString())
        assertEquals(DasherImeTextSink.Result.EDITED, editor.sink.onTextChanged("a"))
        assertEquals("A", editor.text.toString())
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

    @Test
    fun theEndOfALongTextIsNotTheEmptyTextDasherStartedFrom() {
        val long = "Ein Satz. ".repeat(30) + "Ende."
        val read = long.takeLast(201)
        // The cursor was at the start of the editor (nothing loaded) and was put at its end.
        assertFalse(DasherImeTextSink.textBeforeCursorMatches(read, "", 200, knownIsPartial = false))
        // Nor is it a short text that happens to be its end.
        assertFalse(DasherImeTextSink.textBeforeCursorMatches(read, "Ende.", 200, knownIsPartial = false))
        // It is the long text Dasher holds, ...
        assertTrue(DasherImeTextSink.textBeforeCursorMatches(read, long, 200, knownIsPartial = false))
        // ... or the end of the long text it was loaded from.
        assertTrue(DasherImeTextSink.textBeforeCursorMatches(read, long.takeLast(200), 200, knownIsPartial = true))
    }

    @Test
    fun aShortTextMatchesOnlyAsAWhole() {
        assertTrue(DasherImeTextSink.textBeforeCursorMatches("Hallo du", "hallo du", 200, knownIsPartial = false))
        assertFalse(DasherImeTextSink.textBeforeCursorMatches("Hallo du", "du", 200, knownIsPartial = false))
        assertTrue(DasherImeTextSink.textBeforeCursorMatches("Hallo du", "du", 200, knownIsPartial = true))
        assertFalse(DasherImeTextSink.textBeforeCursorMatches("Hallo du", "", 200, knownIsPartial = false))
        assertTrue(DasherImeTextSink.textBeforeCursorMatches("", "", 200, knownIsPartial = false))
        assertTrue(DasherImeTextSink.textBeforeCursorMatches("\u201EJa\u201C\u00A0", "\"Ja\" ", 200, knownIsPartial = false))
    }
}
