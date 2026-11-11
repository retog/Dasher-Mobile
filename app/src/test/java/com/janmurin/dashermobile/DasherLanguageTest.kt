package com.janmurin.dashermobile

import org.junit.Assert.assertEquals
import org.junit.Test

/** Unit tests for the language sets of [DasherLanguage]. */
class DasherLanguageTest {

    @Test
    fun aSetIsWrittenAsItsCodesInAlphabeticalOrder() {
        assertEquals("de+en", DasherLanguage.formatSet(setOf(DasherLanguage.ENGLISH, DasherLanguage.GERMAN)))
        assertEquals("sk", DasherLanguage.formatSet(setOf(DasherLanguage.SLOVAK)))
        assertEquals("en", DasherLanguage.formatSet(emptySet()))
    }

    @Test
    fun aWrittenSetIsReadBack() {
        assertEquals(
            setOf(DasherLanguage.GERMAN, DasherLanguage.ENGLISH, DasherLanguage.FRENCH),
            DasherLanguage.parseSet("de+en+fr")
        )
        // The single language stored before several could be chosen.
        assertEquals(setOf(DasherLanguage.SLOVAK), DasherLanguage.parseSet("sk"))
    }

    @Test
    fun unknownOrMissingLanguagesGiveTheDefault() {
        assertEquals(DasherLanguage.default, DasherLanguage.parseSet(null))
        assertEquals(DasherLanguage.default, DasherLanguage.parseSet(""))
        assertEquals(DasherLanguage.default, DasherLanguage.parseSet("xx"))
        assertEquals(setOf(DasherLanguage.GERMAN), DasherLanguage.parseSet("xx+de"))
    }

    @Test
    fun theVoiceLanguageIsOneOfTheActiveOnes() {
        val active = setOf(DasherLanguage.ITALIAN, DasherLanguage.FRENCH)
        assert(VoiceInput.languageFor(active) in active)
        assertEquals(DasherLanguage.SLOVAK, VoiceInput.languageFor(setOf(DasherLanguage.SLOVAK)))
    }
}
