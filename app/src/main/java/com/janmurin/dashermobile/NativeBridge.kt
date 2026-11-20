package com.janmurin.dashermobile

import android.content.res.AssetManager

/**
 * Thin Kotlin wrapper that exposes the DasherCore JNI library to the rest of the application.
 *
 * Every public method maps 1-to-1 onto a native function implemented in `jni_bridge.cpp`.
 * The native library (`dasher_jni`) is loaded automatically when the class is first referenced.
 *
 * All handle-based functions accept a [Long] that was previously returned by [nativeCreate].
 * Passing an invalid (e.g. already-destroyed) handle is safe: the native layer performs a null
 * check and is a no-op in that case.
 */
class NativeBridge {
    companion object {
        init {
            System.loadLibrary("dasher_jni")
        }

        /** Returns a human-readable version string identifying the bundled DasherCore build. */
        @JvmStatic external fun nativeVersion(): String

        /**
         * Allocates a new native DasherCore session.
         *
         * @param filesDir Absolute path to the application's private files directory.
         *   DasherCore writes its XML settings file here.
         * @param languages The languages to predict for, as written by
         *   [DasherLanguage.formatSet] (e.g. `"de+en"`).  They decide the alphabet and the text
         *   the language model is trained on, and stay the same for the life of the session.
         *   Only one session may exist at a time.
         * @return An opaque handle (non-zero on success, zero on failure) that must be
         *   passed to every subsequent native call and eventually to [nativeDestroy].
         */
        @JvmStatic external fun nativeCreate(filesDir: String, languages: String): Long

        /**
         * Releases all native resources associated with [handle].
         *
         * After this call the handle is invalid and must not be used.
         *
         * @param handle Opaque session handle returned by [nativeCreate].
         */
        @JvmStatic external fun nativeDestroy(handle: Long)

        /**
         * Provides the Android [AssetManager] to the native layer so that DasherCore can read
         * alphabet XML files and other bundled assets from the APK.
         *
         * @param handle Opaque session handle.
         * @param assetManager The application's [AssetManager] instance.
         */
        @JvmStatic external fun nativeSetAssetManager(handle: Long, assetManager: AssetManager)

        /**
         * Notifies the native engine of the current rendering surface dimensions.
         *
         * Must be called whenever the canvas size changes (e.g. on first layout or rotation).
         *
         * @param handle Opaque session handle.
         * @param width  Surface width in pixels.
         * @param height Surface height in pixels.
         */
        @JvmStatic external fun nativeSetScreenSize(handle: Long, width: Int, height: Int)

        /**
         * Delivers a touch event to the DasherCore pointer-input module.
         *
         * @param handle Opaque session handle.
         * @param action Touch action code: `0` = DOWN, `1` = MOVE, `2` = UP/CANCEL.
         * @param x      Touch X coordinate in surface pixels.
         * @param y      Touch Y coordinate in surface pixels.
         */
        @JvmStatic external fun nativeTouch(handle: Long, action: Int, x: Float, y: Float)

        /**
         * Advances the Dasher animation by one frame and returns the resulting draw commands.
         *
         * The returned [IntArray] is a flat list of 6-integer draw-command records:
         * `[op, a, b, c, d, color]` where `op` identifies the primitive type:
         * - `0` – fill canvas with `color`
         * - `1` – circle at `(a, b)` with radius `c`; `d != 0` means filled
         * - `2` – line from `(a, b)` to `(c, d)`
         * - `3` – stroked rectangle from `(a, b)` to `(c, d)`
         * - `4` – filled rectangle from `(a, b)` to `(c, d)`
         *
         * @param handle  Opaque session handle.
         * @param timeMs  Current frame timestamp in milliseconds (typically from Choreographer).
         * @return Flat draw-command array; never null but may be empty.
         */
        @JvmStatic external fun nativeFrame(handle: Long, timeMs: Long): IntArray

        /**
         * Retrieves text strings (e.g. labels on Dasher nodes) that were produced during the
         * most recent [nativeFrame] call and removes them from the internal queue.
         *
         * @param handle Opaque session handle.
         * @return Array of strings indexed to match the text-draw commands in the last frame.
         */
        @JvmStatic external fun nativeGetFrameStrings(handle: Long): Array<String>

        /**
         * Returns the text that DasherCore has output so far in the current session.
         *
         * @param handle Opaque session handle.
         * @return The accumulated output string.
         */
        @JvmStatic external fun nativeGetOutputText(handle: Long): String

        /**
         * Starts a new text inside the native session: saves learned text, clears the
         * output-text buffer and rebuilds the Dasher model with an empty context.
         *
         * @param handle Opaque session handle.
         */
        @JvmStatic external fun nativeResetOutputText(handle: Long)

        /**
         * Appends text written since the last flush to the user training file, so the
         * language model keeps what it learned across sessions.
         *
         * @param handle Opaque session handle.
         */
        @JvmStatic external fun nativeFlushTrainingText(handle: Long)

        /**
         * Turns learning from written text on or off.  Off, the language model is not updated
         * and nothing is saved to the user training file.  A new session starts with it on.
         *
         * @param handle  Opaque session handle.
         * @param enabled `false` for editors whose text must not be learned (passwords).
         */
        @JvmStatic external fun nativeSetLearningEnabled(handle: Long, enabled: Boolean)

        /**
         * Loads the text before an editor's cursor as if it had been written with Dasher, so
         * the next boxes are sized exactly as if the user had just written it.  It is not
         * learned, and only the part after the last character the alphabet lacks is kept.
         *
         * @param handle           Opaque session handle.
         * @param textBeforeCursor Plain text before the cursor.
         * @return The text actually loaded, which is the new output text.
         */
        @JvmStatic external fun nativeSetContext(handle: Long, textBeforeCursor: String): String

        /**
         * Returns the languages the session predicts for, as given to [nativeCreate] but
         * in canonical form: known codes only, in alphabetical order, joined by `+`.
         *
         * @param handle Opaque session handle.
         */
        @JvmStatic external fun nativeGetLanguages(handle: Long): String

        /**
         * Returns the identifier of the DasherCore alphabet generated for a set of languages.
         * KenLM model files are named after it (see [KenlmModelFiles]).
         *
         * @param languages Language set as written by [DasherLanguage.formatSet].
         */
        @JvmStatic external fun nativeAlphabetIdFor(languages: String): String

        /**
         * Returns the numeric ID of the active language model.
         *
         * @param handle Opaque session handle.
         * @return Language-model ID (see [LanguageModel]).
         */
        @JvmStatic external fun nativeGetLanguageModelId(handle: Long): Int

        /**
         * Switches the language model used by DasherCore.
         *
         * @param handle  Opaque session handle.
         * @param modelId Numeric language-model ID (see [LanguageModel]).
         */
        @JvmStatic external fun nativeSetLanguageModelId(handle: Long, modelId: Int)

        /**
         * Returns the movement-speed multiplier as a percentage (100 = normal speed).
         *
         * @param handle Opaque session handle.
         */
        @JvmStatic external fun nativeGetMovementSpeedPercent(handle: Long): Int

        /**
         * Sets the movement-speed multiplier.
         *
         * @param handle  Opaque session handle.
         * @param percent Speed percentage; valid values are 50–400.
         */
        @JvmStatic external fun nativeSetMovementSpeedPercent(handle: Long, percent: Int)

        /**
         * Returns the name of the training text bundled for a language, a file under the
         * `training` assets directory; empty if none is bundled.
         *
         * @param language A [DasherLanguage.code].
         */
        @JvmStatic external fun nativeCorpusAssetFor(language: String): String

        /**
         * Returns the name of the file, in the private files directory, that a session reads
         * as a language's imported training text.
         *
         * @param language A [DasherLanguage.code].
         */
        @JvmStatic external fun nativeImportedCorpusFileFor(language: String): String

        /**
         * Starts moving into the box of a symbol, as if the user had steered there: over the
         * next frames the view scrolls level with the box and zooms into it, which writes the
         * symbol.  Used for spoken letters.  Call [nativeFrame] until [nativeIsSteering]
         * returns `false`; a touch cancels the move.
         *
         * @param handle Opaque session handle.
         * @param text   What the symbol types: a letter, a punctuation mark, `" "` or `"\n"`.
         * @return `false` if the alphabet has no such symbol.
         */
        @JvmStatic external fun nativeSteerToSymbol(handle: Long, text: String): Boolean

        /**
         * Starts moving back out of the box of the symbol written last, which deletes it.
         *
         * @param handle Opaque session handle.
         * @return `false` if the session holds no written symbol to move out of.
         */
        @JvmStatic external fun nativeSteerBack(handle: Long): Boolean

        /**
         * Whether a move started by [nativeSteerToSymbol] or [nativeSteerBack] is under way.
         *
         * @param handle Opaque session handle.
         */
        @JvmStatic external fun nativeIsSteering(handle: Long): Boolean

        /**
         * Lists the personal dictionary, the most often written entries first.
         *
         * @param filesDir The application's private files directory.
         * @return One string per entry: count, `1` if added by hand else `0`, language set
         *   (empty for every language) and text, separated by tabs.
         */
        @JvmStatic external fun nativeDictionaryEntries(filesDir: String): Array<String>

        /**
         * Adds a word or phrase to the personal dictionary by hand.
         *
         * @param languages Language set the entry is for, or empty for every language.
         * @return `false` if [text] holds no word or the dictionary cannot be saved.
         */
        @JvmStatic external fun nativeDictionaryAdd(filesDir: String, text: String, languages: String): Boolean

        /** Removes an entry from the personal dictionary; `false` if there was none. */
        @JvmStatic external fun nativeDictionaryRemove(filesDir: String, text: String, languages: String): Boolean

        /**
         * Counts the words and phrases of text the user entered without Dasher (dictation)
         * in the personal dictionary.
         *
         * @param languages Language set that was active.
         */
        @JvmStatic external fun nativeDictionaryLearn(filesDir: String, text: String, languages: String)

        /**
         * Deletes everything learned from the user: the text written with Dasher and the
         * learned entries of the personal dictionary.  Entries added by hand stay.  A running
         * session keeps what it has learned until it is recreated.
         */
        @JvmStatic external fun nativeForgetLearned(filesDir: String)
    }
}
