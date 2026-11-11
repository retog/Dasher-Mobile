package com.janmurin.dashermobile

import java.util.Locale

/**
 * Turns what a speech recogniser heard into the symbols Dasher is to move to.
 *
 * In the "spoken letters" voice mode the user says one letter after the other (or a few in a
 * row), and Dasher moves into the box of each.  A recogniser is made for words, not letters,
 * so what it returns for a spoken "b" may be `"B"`, `"be"` or `"bee"`.  An utterance is
 * therefore read word by word as
 *
 * - a single character: the letter or punctuation mark itself,
 * - the name of a letter in the language spoken ("bee", "zed"; "ypsilon", "eszett"),
 * - a word of a spelling alphabet ("bravo", "Berta"),
 * - the name of a punctuation mark or of the space ("comma", "space"; "Punkt", "Leerzeichen"),
 * - a command to go back one symbol ("back"; "zurück"),
 * - a word for upper case before a letter ("capital b"; "gross b").
 *
 * A recogniser offers several alternatives for what it heard.  The first one in which every
 * word can be read this way is taken; if there is none, nothing is written, as a word that is
 * no letter is more likely a misheard letter than something to spell out.
 */
object SpokenLetters {

    private class Vocabulary(
        val letters: Map<String, String>,
        val symbols: Map<String, String>,
        val back: Set<String>,
        val capital: Set<String>
    )

    /** Spelling alphabets understood in every language: the NATO one and the German one. */
    private val spellingWords: Map<String, String> = mapOf(
        "alfa" to "a", "alpha" to "a", "bravo" to "b", "charlie" to "c", "delta" to "d",
        "echo" to "e", "foxtrot" to "f", "golf" to "g", "hotel" to "h", "india" to "i",
        "juliett" to "j", "juliet" to "j", "kilo" to "k", "lima" to "l", "mike" to "m",
        "november" to "n", "oscar" to "o", "papa" to "p", "quebec" to "q", "romeo" to "r",
        "sierra" to "s", "tango" to "t", "uniform" to "u", "victor" to "v", "whiskey" to "w",
        "whisky" to "w", "x-ray" to "x", "xray" to "x", "yankee" to "y", "zulu" to "z",
        "anton" to "a", "berta" to "b", "cäsar" to "c", "dora" to "d", "emil" to "e",
        "friedrich" to "f", "gustav" to "g", "heinrich" to "h", "ida" to "i", "julius" to "j",
        "kaufmann" to "k", "ludwig" to "l", "martha" to "m", "nordpol" to "n", "otto" to "o",
        "paula" to "p", "quelle" to "q", "richard" to "r", "samuel" to "s", "siegfried" to "s",
        "theodor" to "t", "ulrich" to "u", "viktor" to "v", "wilhelm" to "w", "xanthippe" to "x",
        "ypsilon" to "y", "zacharias" to "z", "zeppelin" to "z", "ärger" to "ä", "ökonom" to "ö",
        "übermut" to "ü"
    )

    private val english = Vocabulary(
        letters = mapOf(
            "ay" to "a", "bee" to "b", "be" to "b", "see" to "c", "sea" to "c", "cee" to "c",
            "dee" to "d", "ee" to "e", "ef" to "f", "eff" to "f", "gee" to "g", "aitch" to "h",
            "eye" to "i", "jay" to "j", "kay" to "k", "el" to "l", "ell" to "l", "em" to "m",
            "en" to "n", "oh" to "o", "pee" to "p", "pea" to "p", "cue" to "q", "queue" to "q",
            "ar" to "r", "are" to "r", "es" to "s", "ess" to "s", "tee" to "t", "tea" to "t",
            "you" to "u", "vee" to "v", "double u" to "w", "double you" to "w", "ex" to "x",
            "why" to "y", "wye" to "y", "zed" to "z", "zee" to "z"
        ),
        symbols = mapOf(
            "space" to " ", "new line" to "\n", "newline" to "\n", "enter" to "\n",
            "period" to ".", "full stop" to ".", "dot" to ".", "comma" to ",",
            "question mark" to "?", "exclamation mark" to "!", "exclamation point" to "!",
            "colon" to ":", "semicolon" to ";", "hyphen" to "-", "dash" to "-",
            "apostrophe" to "'", "quote" to "\""
        ),
        back = setOf("back", "delete", "backspace", "undo"),
        capital = setOf("capital", "uppercase", "upper case", "big")
    )

    private val german = Vocabulary(
        letters = mapOf(
            "ah" to "a", "be" to "b", "bee" to "b", "ce" to "c", "tse" to "c", "zeh" to "c",
            "de" to "d", "deh" to "d", "eh" to "e", "ef" to "f", "eff" to "f", "ge" to "g",
            "geh" to "g", "ha" to "h", "hah" to "h", "ih" to "i", "jot" to "j", "jott" to "j",
            "ka" to "k", "kah" to "k", "el" to "l", "ell" to "l", "em" to "m", "emm" to "m",
            "en" to "n", "enn" to "n", "oh" to "o", "pe" to "p", "peh" to "p", "ku" to "q",
            "kuh" to "q", "qu" to "q", "er" to "r", "err" to "r", "es" to "s", "ess" to "s",
            "te" to "t", "teh" to "t", "tee" to "t", "uh" to "u", "fau" to "v", "vau" to "v",
            "we" to "w", "weh" to "w", "ix" to "x", "iks" to "x", "üpsilon" to "y",
            "zet" to "z", "zett" to "z", "äh" to "ä", "öh" to "ö", "üh" to "ü",
            "eszett" to "ß", "es zett" to "ß", "scharfes s" to "ß"
        ),
        symbols = mapOf(
            "leerzeichen" to " ", "leerschlag" to " ", "neue zeile" to "\n", "absatz" to "\n",
            "zeilenumbruch" to "\n", "punkt" to ".", "komma" to ",", "fragezeichen" to "?",
            "ausrufezeichen" to "!", "doppelpunkt" to ":", "strichpunkt" to ";",
            "semikolon" to ";", "bindestrich" to "-", "minus" to "-", "apostroph" to "'",
            "anführungszeichen" to "\""
        ),
        back = setOf("zurück", "löschen"),
        capital = setOf("gross", "groß", "grosses", "großes")
    )

    private val french = Vocabulary(
        letters = emptyMap(),
        symbols = mapOf(
            "espace" to " ", "à la ligne" to "\n", "point" to ".", "virgule" to ",",
            "point d'interrogation" to "?", "point d'exclamation" to "!"
        ),
        back = setOf("retour", "effacer"),
        capital = setOf("majuscule")
    )

    private val italian = Vocabulary(
        letters = emptyMap(),
        symbols = mapOf(
            "spazio" to " ", "a capo" to "\n", "punto" to ".", "virgola" to ",",
            "punto interrogativo" to "?", "punto esclamativo" to "!"
        ),
        back = setOf("indietro", "cancella"),
        capital = setOf("maiuscola", "maiuscolo")
    )

    private val spanish = Vocabulary(
        letters = emptyMap(),
        symbols = mapOf(
            "espacio" to " ", "nueva línea" to "\n", "punto" to ".", "coma" to ","
        ),
        back = setOf("atrás", "borrar"),
        capital = setOf("mayúscula")
    )

    private val slovak = Vocabulary(
        letters = emptyMap(),
        symbols = mapOf(
            "medzera" to " ", "nový riadok" to "\n", "bodka" to ".", "čiarka" to ",",
            "otáznik" to "?", "výkričník" to "!"
        ),
        back = setOf("späť", "zmazať"),
        capital = setOf("veľké")
    )

    private fun vocabularyFor(language: DasherLanguage): Vocabulary = when (language) {
        DasherLanguage.ENGLISH -> english
        DasherLanguage.GERMAN -> german
        DasherLanguage.FRENCH -> french
        DasherLanguage.ITALIAN -> italian
        DasherLanguage.SPANISH -> spanish
        DasherLanguage.SLOVAK -> slovak
    }

    /** Punctuation a recogniser may return as itself. */
    private const val PUNCTUATION = ".,?!'\"-:;()¿¡"

    /** The longest name of a letter, symbol or command, in words. */
    private const val MAX_NAME_WORDS = 3

    /**
     * Reads the alternatives a recogniser returned for one utterance.
     *
     * @param alternatives What the recogniser heard, the most likely first.
     * @param language     The language that was recognised.
     * @return What to give to [DasherEngine.steer]: a symbol for each letter, punctuation mark
     *   or space, and [DasherEngine.STEER_BACK] for each command to go back.  Empty if no
     *   alternative can be read as letters.
     */
    fun parse(alternatives: List<String>, language: DasherLanguage): List<String> {
        val vocabulary = vocabularyFor(language)
        for (alternative in alternatives) {
            val symbols = read(alternative, vocabulary, language)
            if (!symbols.isNullOrEmpty()) return symbols
        }
        return emptyList()
    }

    private fun read(utterance: String, vocabulary: Vocabulary, language: DasherLanguage): List<String>? {
        val locale = Locale(language.code)
        val words = utterance.trim().lowercase(locale).split(Regex("\\s+")).filter { it.isNotEmpty() }
        val symbols = mutableListOf<String>()
        var capital = false
        var index = 0
        while (index < words.size) {
            // The longest name starting here wins: "question mark" before "question".
            var matched = false
            for (length in minOf(MAX_NAME_WORDS, words.size - index) downTo 1) {
                val name = words.subList(index, index + length).joinToString(" ")
                val bare = name.trim('.', ',', '!', '?')
                val key = if (length == 1 && name.length == 1) name else bare
                if (key in vocabulary.capital) {
                    capital = true
                } else if (key in vocabulary.back) {
                    symbols += DasherEngine.STEER_BACK
                    capital = false
                } else {
                    val symbol = vocabulary.symbols[key]
                        ?: letter(key, vocabulary)
                        ?: continue
                    symbols += if (capital) symbol.uppercase(locale) else symbol
                    capital = false
                }
                index += length
                matched = true
                break
            }
            if (!matched) return null
        }
        // A word for upper case must be followed by its letter.
        return if (capital) null else symbols
    }

    private fun letter(name: String, vocabulary: Vocabulary): String? {
        if (name.length == 1) {
            val char = name[0]
            return if (char.isLetter() || char in PUNCTUATION) name else null
        }
        return vocabulary.letters[name] ?: spellingWords[name]
    }
}
