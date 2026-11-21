package com.janmurin.dashermobile

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.util.Log
import androidx.core.content.ContextCompat
import java.util.Locale

/**
 * The two ways of entering text by voice.
 *
 * - [DICTATION] – what the user says is entered as text, as with a keyboard's microphone key.
 * - [LETTERS]   – the user says single letters, and Dasher moves into the box of each
 *   (see [SpokenLetters]).
 */
enum class VoiceMode {
    DICTATION,
    LETTERS
}

/**
 * Listens to the user through Android's [SpeechRecognizer] for as long as a [VoiceMode] is on.
 *
 * The recogniser hears one utterance at a time; this class starts it again after each, so the
 * user can go on speaking until the mode is switched off with [stop].  It needs the
 * [Manifest.permission.RECORD_AUDIO] permission; without it [start] calls
 * [Listener.onVoicePermissionNeeded] and does nothing else.
 *
 * All methods must be called on the main thread, where the callbacks arrive as well.
 */
class VoiceInput(private val context: Context, private val listener: Listener) {

    interface Listener {
        /** The mode that is now on, or `null` when listening has ended. */
        fun onVoiceModeChanged(mode: VoiceMode?)

        /**
         * The user said something.
         *
         * @param mode         The mode that is on.
         * @param alternatives What the recogniser heard, the most likely first; never empty.
         * @param language     The language that was recognised.
         */
        fun onVoiceHeard(mode: VoiceMode, alternatives: List<String>, language: DasherLanguage)

        /** The microphone may not be used yet; the host should ask for the permission. */
        fun onVoicePermissionNeeded()

        /**
         * Listening ended because speech cannot be recognised.
         *
         * @param error The [SpeechRecognizer] error the recogniser kept reporting, or
         *   [NO_RECOGNIZER] if the device has no recognition service.
         */
        fun onVoiceUnavailable(error: Int)
    }

    companion object {
        private const val TAG = "VoiceInput"
        /** Given to [Listener.onVoiceUnavailable] when the device has no recognition service. */
        const val NO_RECOGNIZER = -1
        private const val RESTART_DELAY_MS = 150L
        /** Failed attempts to listen in a row after which listening ends. */
        private const val MAX_FAILURES = 4

        /**
         * The language to recognise: the device's if it is among the active ones, as that is
         * the one the user most likely speaks, else the first active one.
         */
        fun languageFor(active: Set<DasherLanguage>): DasherLanguage {
            val device = Locale.getDefault().language
            return active.firstOrNull { it.code == device }
                ?: active.minByOrNull { it.code }
                ?: DasherLanguage.ENGLISH
        }
    }

    private val handler = Handler(Looper.getMainLooper())
    private var recognizer: SpeechRecognizer? = null
    private var language = DasherLanguage.ENGLISH
    private var failures = 0
    private val listenAgain = Runnable { listen() }

    /** The mode that is on, or `null` while not listening. */
    var mode: VoiceMode? = null
        private set

    /**
     * Switches a voice mode on and starts listening.
     *
     * @param mode     The mode to switch on; replaces the one that is on.
     * @param language The language to recognise.
     */
    fun start(mode: VoiceMode, language: DasherLanguage) {
        if (!mayRecord()) {
            listener.onVoicePermissionNeeded()
            return
        }
        if (!SpeechRecognizer.isRecognitionAvailable(context)) {
            listener.onVoiceUnavailable(NO_RECOGNIZER)
            return
        }
        this.language = language
        this.mode = mode
        failures = 0
        listener.onVoiceModeChanged(mode)
        listen()
    }

    /** Switches the voice mode off and stops listening. */
    fun stop() {
        handler.removeCallbacks(listenAgain)
        recognizer?.cancel()
        if (mode != null) {
            mode = null
            listener.onVoiceModeChanged(null)
        }
    }

    /** Stops listening and releases the recogniser. */
    fun destroy() {
        stop()
        recognizer?.destroy()
        recognizer = null
    }

    private fun mayRecord(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED

    private fun listen() {
        val current = mode ?: return
        val active = recognizer ?: SpeechRecognizer.createSpeechRecognizer(context).also {
            it.setRecognitionListener(recognitionListener)
            recognizer = it
        }
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE, language.code)
            putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, context.packageName)
            // A spoken letter is easily misheard; the alternatives often hold the right one.
            putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, if (current == VoiceMode.LETTERS) 5 else 1)
        }
        try {
            active.startListening(intent)
        } catch (e: SecurityException) {
            Log.w(TAG, "may not listen", e)
            stop()
            listener.onVoicePermissionNeeded()
        }
    }

    private fun listenAgainSoon() {
        if (mode == null) return
        handler.removeCallbacks(listenAgain)
        handler.postDelayed(listenAgain, RESTART_DELAY_MS)
    }

    private val recognitionListener = object : RecognitionListener {
        override fun onResults(results: Bundle?) {
            val current = mode ?: return
            failures = 0
            val heard = results?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                ?.filter { it.isNotBlank() }
                .orEmpty()
            if (heard.isNotEmpty()) {
                listener.onVoiceHeard(current, heard, language)
            }
            listenAgainSoon()
        }

        override fun onError(error: Int) {
            if (mode == null) return
            when (error) {
                // Nothing was said, or nothing understood: keep listening.
                SpeechRecognizer.ERROR_NO_MATCH, SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> listenAgainSoon()
                else -> {
                    Log.w(TAG, "recognition error $error")
                    if (error == SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS && !mayRecord()) {
                        stop()
                        listener.onVoicePermissionNeeded()
                    } else if (error == SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS || ++failures >= MAX_FAILURES) {
                        // With the permission granted, the recogniser itself may not listen,
                        // and asking the user again would not change that.
                        stop()
                        listener.onVoiceUnavailable(error)
                    } else {
                        listenAgainSoon()
                    }
                }
            }
        }

        override fun onReadyForSpeech(params: Bundle?) {
            // Listening again: an earlier failure was a passing one.
            failures = 0
        }

        override fun onBeginningOfSpeech() = Unit
        override fun onRmsChanged(rmsdB: Float) = Unit
        override fun onBufferReceived(buffer: ByteArray?) = Unit
        override fun onEndOfSpeech() = Unit
        override fun onPartialResults(partialResults: Bundle?) = Unit
        override fun onEvent(eventType: Int, params: Bundle?) = Unit
    }
}

/**
 * What to tell the user when [VoiceInput.Listener.onVoiceUnavailable] is called.  The error
 * number is the [SpeechRecognizer] one, which says why the recogniser did not listen.
 */
fun voiceUnavailableMessage(context: Context, error: Int): String =
    if (error == VoiceInput.NO_RECOGNIZER) {
        context.getString(R.string.voice_no_recognizer)
    } else {
        context.getString(R.string.voice_unavailable, error)
    }
