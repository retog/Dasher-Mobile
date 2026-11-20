package com.janmurin.dashermobile

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File

/**
 * Utility object for managing KenLM binary language-model files on the device.
 *
 * KenLM models are large binary files (`.binary` / `.bin`) that the user must import manually
 * from device storage via the Settings screen.  Once imported they live in the app's private
 * [android.content.Context.getFilesDir] under a predictable name derived from the languages.
 *
 * File naming convention: `kenlm_<alphabetId>[.binary|.bin]`, where the alphabet ID is that of
 * the alphabet generated for the active languages: `English with limited punctuation` for
 * English alone, `Slovak` for Slovak alone, and otherwise `Dasher Mobile ` followed by the
 * language set, e.g. `Dasher Mobile de+en`.
 *
 * Every function takes the language set as written by [DasherLanguage.formatSet].
 *
 * @see DasherLanguage
 */
object KenlmModelFiles {
    private const val MODEL_EXTENSION_BINARY = ".binary"
    private const val MODEL_EXTENSION_BIN = ".bin"

    private fun modelBaseName(languages: String): String =
        "kenlm_${NativeBridge.nativeAlphabetIdFor(languages)}"

    /**
     * Returns the list of acceptable file names for a KenLM model for [languages].
     *
     * Both `.binary` (preferred) and `.bin` extensions are accepted so that models exported
     * by the standard KenLM toolchain work without renaming.
     *
     * @param languages The target language set.
     * @return Ordered list of acceptable file name strings.
     */
    fun expectedFileNames(languages: String): List<String> {
        val base = modelBaseName(languages)
        return listOf("$base$MODEL_EXTENSION_BINARY", "$base$MODEL_EXTENSION_BIN")
    }

    /**
     * Returns `true` if a KenLM model file for [languages] already exists in the app's
     * private files directory.
     *
     * @param context  Any Android context.
     * @param languages The language set to check.
     */
    fun hasModel(context: Context, languages: String): Boolean {
        return expectedFileNames(languages).any { fileName ->
            File(context.filesDir, fileName).isFile
        }
    }

    private fun sourceDisplayName(context: Context, sourceUri: Uri): String? {
        context.contentResolver.query(sourceUri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (nameIndex >= 0) {
                        return cursor.getString(nameIndex)
                    }
                }
            }
        return sourceUri.lastPathSegment?.substringAfterLast('/')
    }

    /**
     * Checks whether the file identified by [sourceUri] has a valid KenLM model name for
     * [languages].
     *
     * The check is case-insensitive and is based solely on the display name of the URI; it
     * does not validate the file's binary content.
     *
     * @param context   Any Android context.
     * @param languages The expected target language set.
     * @param sourceUri URI pointing to the candidate file (e.g. from a file-picker).
     * @return `true` if the file name matches one of [expectedFileNames].
     */
    fun hasValidModelFileName(context: Context, languages: String, sourceUri: Uri): Boolean {
        val fileName = sourceDisplayName(context, sourceUri) ?: return false
        return expectedFileNames(languages).any { expected ->
            expected.equals(fileName, ignoreCase = true)
        }
    }

    /**
     * Copies the KenLM model identified by [sourceUri] into the app's private files directory.
     *
     * The file is written as `kenlm_<alphabetId>.binary` regardless of the source extension.
     * The import fails (returns `false`) if [hasValidModelFileName] is `false` for the
     * supplied URI.
     *
     * @param context   Any Android context with read permission to [sourceUri].
     * @param languages The language set the model belongs to.
     * @param sourceUri URI of the source model file.
     * @return `true` on success, `false` if the file name is invalid or an I/O error occurs.
     */
    fun importModel(context: Context, languages: String, sourceUri: Uri): Boolean {
        if (!hasValidModelFileName(context, languages, sourceUri)) return false
        val input = context.contentResolver.openInputStream(sourceUri) ?: return false
        return try {
            val target = File(context.filesDir, "${modelBaseName(languages)}$MODEL_EXTENSION_BINARY")
            input.use { source ->
                target.outputStream().use { output ->
                    source.copyTo(output)
                }
            }
            true
        } catch (_: Exception) {
            false
        }
    }
}
