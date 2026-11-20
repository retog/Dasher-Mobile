package com.janmurin.dashermobile

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Unit tests for [SpokenLetters]: what a speech recogniser heard, read as the symbols Dasher is
 * to move to.
 */
class SpokenLettersTest {

    private fun english(vararg alternatives: String) =
        SpokenLetters.parse(alternatives.toList(), DasherLanguage.ENGLISH)

    private fun german(vararg alternatives: String) =
        SpokenLetters.parse(alternatives.toList(), DasherLanguage.GERMAN)

    @Test
    fun aSingleLetterIsItself() {
        // Recognisers write a lone letter in upper case; it is a lower-case letter all the same.
        assertEquals(listOf("b"), english("B"))
        assertEquals(listOf("ü"), german("Ü"))
        assertEquals(listOf("a"), english("a."))
    }

    @Test
    fun letterNamesAreReadInTheLanguageSpoken() {
        assertEquals(listOf("b"), english("bee"))
        assertEquals(listOf("r"), english("are"))
        assertEquals(listOf("w"), english("double you"))
        assertEquals(listOf("z"), english("zed"))
        assertEquals(listOf("y"), german("Ypsilon"))
        assertEquals(listOf("ß"), german("scharfes S"))
        assertEquals(listOf("v"), german("Vau"))
        // "we" names a letter in German, but is just a word in English.
        assertEquals(listOf("w"), german("we"))
        assertEquals(emptyList<String>(), english("we"))
    }

    @Test
    fun spellingAlphabetsWorkInEveryLanguage() {
        assertEquals(listOf("b"), english("Bravo"))
        assertEquals(listOf("b"), german("Berta"))
        assertEquals(listOf("k"), SpokenLetters.parse(listOf("kilo"), DasherLanguage.SLOVAK))
    }

    @Test
    fun severalLettersInOneUtterance() {
        assertEquals(listOf("h", "a", "l", "l", "o"), german("h a l l o"))
        assertEquals(listOf("c", "a", "b"), english("see a bee"))
    }

    @Test
    fun spaceAndPunctuationHaveNames() {
        assertEquals(listOf(" "), english("space"))
        assertEquals(listOf(" "), german("Leerzeichen"))
        assertEquals(listOf("?"), english("question mark"))
        assertEquals(listOf("a", ",", " ", "b"), english("a comma space b"))
        assertEquals(listOf("."), german("Punkt"))
        assertEquals(listOf("\n"), german("neue Zeile"))
        assertEquals(listOf("?"), english("?"))
        assertEquals(listOf(" "), SpokenLetters.parse(listOf("espace"), DasherLanguage.FRENCH))
    }

    @Test
    fun goingBack() {
        assertEquals(listOf(DasherEngine.STEER_BACK), english("back"))
        assertEquals(listOf(DasherEngine.STEER_BACK, DasherEngine.STEER_BACK), german("zurück zurück"))
        assertEquals(listOf("a", DasherEngine.STEER_BACK, "b"), english("a delete b"))
    }

    @Test
    fun upperCaseTakesAWordBeforeTheLetter() {
        assertEquals(listOf("B"), english("capital b"))
        assertEquals(listOf("B", "a"), english("capital bee a"))
        assertEquals(listOf("Ä"), german("großes ä"))
        assertEquals(listOf("Z"), german("gross Zett"))
        // The word alone says nothing.
        assertEquals(emptyList<String>(), english("capital"))
    }

    @Test
    fun wordsThatAreNoLettersWriteNothing() {
        assertEquals(emptyList<String>(), english("hello"))
        assertEquals(emptyList<String>(), english("a hello"))
        assertEquals(emptyList<String>(), english(""))
        assertEquals(emptyList<String>(), SpokenLetters.parse(emptyList(), DasherLanguage.ENGLISH))
    }

    @Test
    fun theFirstAlternativeThatReadsAsLettersIsTaken() {
        // The recogniser's best guess is a word, its second the letter that was said.
        assertEquals(listOf("p"), english("pay", "pee", "P"))
        assertEquals(listOf("t"), german("Tee trinken", "T"))
    }
}
