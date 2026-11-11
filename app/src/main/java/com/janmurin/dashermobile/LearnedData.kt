package com.janmurin.dashermobile

import android.content.Context
import android.net.Uri
import java.io.File

/**
 * The text a language's letter probabilities are first learned from.
 *
 * A session is trained on one such text per active language: the one bundled with the app, if
 * any, and the one the user imported.  Without either, a language is learned only from what
 * the user writes.
 */
object TrainingTexts {
    enum class Source {
        /** Only what the user writes. */
        NONE,
        /** A text that comes with the app. */
        BUNDLED,
        /** A text the user imported, used besides a bundled one. */
        IMPORTED
    }

    private fun importedFile(context: Context, language: DasherLanguage): File =
        File(context.filesDir, NativeBridge.nativeImportedCorpusFileFor(language.code))

    /** Where the training text of [language] comes from. */
    fun source(context: Context, language: DasherLanguage): Source = when {
        importedFile(context, language).isFile -> Source.IMPORTED
        NativeBridge.nativeCorpusAssetFor(language.code).isNotEmpty() -> Source.BUNDLED
        else -> Source.NONE
    }

    /**
     * Copies a plain text file (UTF-8) into the app as the training text of [language],
     * replacing one imported before.  Sessions created afterwards are trained on it.
     *
     * @return `true` on success, `false` if the file is empty or cannot be read.
     */
    fun import(context: Context, language: DasherLanguage, sourceUri: Uri): Boolean {
        val target = importedFile(context, language)
        val temporary = File(target.path + ".tmp")
        return try {
            val input = context.contentResolver.openInputStream(sourceUri) ?: return false
            input.use { source -> temporary.outputStream().use { source.copyTo(it) } }
            val imported = temporary.length() > 0 && temporary.renameTo(target)
            if (imported) DasherSessionCoordinator.invalidateEngine()
            imported
        } catch (_: Exception) {
            false
        } finally {
            temporary.delete()
        }
    }
}

/**
 * The user's own words and phrases, kept by the native layer across sessions
 * (`PersonalDictionary.h`).
 *
 * Words and short phrases the user writes are counted; the ones written often are added, as
 * often as they were written, to the text a session is trained on, so their boxes grow.  The
 * user can add entries by hand.  An entry belongs to the languages that were active when it
 * was written and is used whenever one of them is active; an entry added by hand can also be
 * for every language.
 */
object PersonalDictionary {
    /**
     * @property text      A word, or two to three words.
     * @property languages The language set the entry belongs to, as written by
     *   [DasherLanguage.formatSet]; empty for every language.
     * @property count     How often the user wrote it.
     * @property manual    Whether the user added it by hand.
     */
    data class Entry(val text: String, val languages: String, val count: Int, val manual: Boolean)

    /** All entries, the most often written first. */
    fun entries(context: Context): List<Entry> =
        NativeBridge.nativeDictionaryEntries(context.filesDir.absolutePath).mapNotNull { line ->
            val fields = line.split('\t', limit = 4)
            if (fields.size != 4) return@mapNotNull null
            Entry(
                text = fields[3],
                languages = fields[2],
                count = fields[0].toIntOrNull() ?: 0,
                manual = fields[1] == "1"
            )
        }

    /**
     * Adds a word or phrase by hand.
     *
     * @param languages The language set it is for, or empty for every language.
     * @return `false` if [text] holds no word.
     */
    fun add(context: Context, text: String, languages: String): Boolean {
        val added = NativeBridge.nativeDictionaryAdd(context.filesDir.absolutePath, text, languages)
        if (added) DasherSessionCoordinator.invalidateEngine()
        return added
    }

    /** Removes an entry. */
    fun remove(context: Context, entry: Entry) {
        NativeBridge.nativeDictionaryRemove(context.filesDir.absolutePath, entry.text, entry.languages)
        DasherSessionCoordinator.invalidateEngine()
    }

    /**
     * Deletes everything learned from the user's writing: the learned entries and the text
     * sessions are trained on.  Entries added by hand stay.
     */
    fun forgetLearned(context: Context) {
        NativeBridge.nativeForgetLearned(context.filesDir.absolutePath)
        DasherSessionCoordinator.invalidateEngine()
    }
}
