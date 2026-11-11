package com.janmurin.dashermobile

import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.enableEdgeToEdge
import androidx.core.content.res.ResourcesCompat
import com.janmurin.dashermobile.ui.DasherCanvasView
import com.janmurin.dashermobile.ui.DasherHostUi

/**
 * Standalone full-screen activity that hosts the Dasher writing interface.
 *
 * ## Lifecycle overview
 * ```
 * onCreate  → registers with DasherSessionCoordinator, builds the UI, wires callbacks
 * onResume  → activates the host, starts the frame loop, optionally restores tilt mode
 * onPause   → requests pause, deactivates the host
 * onDestroy → unregisters the host, releases tilt sensor
 * ```
 *
 * ## Features beyond the IME
 * - Full-resolution landscape/portrait layout with larger text and controls.
 * - `FLAG_KEEP_SCREEN_ON` is set while tilt mode is active to prevent the screen from
 *   dimming during hands-free use.
 * - Startup tilt restore: if the user last closed the app in tilt mode the activity will
 *   automatically switch back to tilt and calibrate after the surface is ready.
 * - Opens [SettingsActivity] when the settings button is tapped.
 */
class MainActivity : ComponentActivity() {

    private var hostHandle: DasherSessionCoordinator.HostHandle? = null
    private lateinit var canvasView: DasherCanvasView
    private var tiltProvider: TiltInputProvider? = null
    private var isResumed = false
    private var suppressModeSwitchCallback = false
    private var startupModeApplied = false
    private var pendingStartupTiltRestore = false
    private var voiceInput: VoiceInput? = null
    private var outputView: TextView? = null
    /** Dictated text the engine could not take in (see [appendDictation]); shown before its text. */
    private var textOutsideEngine = ""
    private var engineText = ""

    private fun updateKeepScreenOn(mode: InputMode) {
        if (mode == InputMode.TILT) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    private fun scheduleStartupTiltRestore() {
        window.decorView.post {
            val localHost = hostHandle ?: return@post
            if (!isResumed || !pendingStartupTiltRestore) {
                return@post
            }
            val switched = DasherSessionCoordinator.setInputMode(localHost, InputMode.TILT)
            if (!switched) {
                return@post
            }
            pendingStartupTiltRestore = false
            tiltProvider?.calibrate()
            if (DasherSessionCoordinator.isPaused()) {
                DasherSessionCoordinator.unpause(localHost)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val version = NativeBridge.nativeVersion()
        Log.i("DasherMobile", "JNI loaded: $version")

        hostHandle = DasherSessionCoordinator.registerHost(filesDir.absolutePath, assets, languagesPref())
        val hostViews = DasherHostUi.create(this, HostMode.APP)
        canvasView = hostViews.canvasView
        val textView = hostViews.outputView
        outputView = textView
        val statusView = hostViews.statusView
        val modeSwitch = requireNotNull(hostViews.modeSwitch)
        val calibrateButton = requireNotNull(hostViews.calibrateButton)
        val canvasFrame = hostViews.canvasFrame
        val canvasCalibrateButton = hostViews.canvasCalibrateButton
        canvasCalibrateButton.visibility = View.GONE

        val pausedView = TextView(this).apply {
            text = getString(R.string.paused)
            setTextColor(0xFF000000.toInt())
            textSize = 22f
            typeface = ResourcesCompat.getFont(this@MainActivity, R.font.inter_semibold_italic)
            val padding = (16 * resources.displayMetrics.density).toInt()
            setPadding(padding, padding, padding, padding)
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                gravity = Gravity.TOP or Gravity.START
            }
            visibility = View.GONE
        }
        canvasFrame.addView(pausedView)

        setContentView(hostViews.root)
        hostViews.settingsButton?.setOnClickListener {
            hostHandle?.let { DasherSessionCoordinator.requestPause(it) }
            startActivity(Intent(this, SettingsActivity::class.java))
        }
        enableEdgeToEdge()

        val localHost = hostHandle
        if (localHost != null) {
            tiltProvider = TiltInputProvider(this) { nx, ny ->
                DasherSessionCoordinator.onTiltNormalized(localHost, nx, ny)
            }
            val tiltAvailable = tiltProvider?.hasSensor() == true
            pendingStartupTiltRestore = tiltAvailable && DasherPrefs.getInputMode(this) == InputMode.TILT
            if (!tiltAvailable) {
                modeSwitch.isEnabled = false
                calibrateButton.isEnabled = false
            }

            DasherSessionCoordinator.updateHostCallbacks(
                host = localHost,
                frameConsumer = { commands, strings -> canvasView.submitFrame(commands, strings) },
                textConsumer = { text ->
                    engineText = text
                    textView.text = textOutsideEngine + text
                },
                statusConsumer = { mode, paused ->
                    val modeLabel = if (mode == InputMode.TILT) "TILT" else "TOUCH"
                    val stateLabel = if (paused) "PAUSED" else "RUNNING"
                    statusView.text = getString(R.string.status_label, modeLabel, stateLabel)
                    // Moving by spoken letters, the engine is paused between two letters only.
                    val showPaused = paused && voiceInput?.mode != VoiceMode.LETTERS
                    canvasView.showPauseOverlay = showPaused
                    pausedView.visibility = if (showPaused) View.VISIBLE else View.GONE
                    hostViews.dictationButton.visibility = if (paused) View.VISIBLE else View.GONE
                    hostViews.lettersButton.visibility = if (paused) View.VISIBLE else View.GONE
                    val showCanvasCalibrateButton = paused && mode == InputMode.TILT
                    canvasCalibrateButton.visibility = if (showCanvasCalibrateButton) View.VISIBLE else View.GONE
                    canvasCalibrateButton.isEnabled = showCanvasCalibrateButton
                    updateKeepScreenOn(mode)
                    val switchChecked = mode == InputMode.TILT
                    if (modeSwitch.isChecked != switchChecked) {
                        suppressModeSwitchCallback = true
                        modeSwitch.isChecked = switchChecked
                        suppressModeSwitchCallback = false
                    }
                    calibrateButton.isEnabled = switchChecked
                    if (mode == InputMode.TILT && isResumed) {
                        if (!paused) tiltProvider?.register() else tiltProvider?.unregister()
                    }
                }
            )

            canvasView.onSurfaceSizeChanged = { width, height ->
                DasherSessionCoordinator.onSurfaceSizeChanged(localHost, width, height)
            }
            canvasView.onTouchInput = { action, x, y ->
                DasherSessionCoordinator.onTouch(localHost, action, x, y)
            }

            canvasCalibrateButton.setOnClickListener {
                tiltProvider?.calibrate()
                hostHandle?.let { DasherSessionCoordinator.unpause(it) }
            }

            textView.setOnClickListener {
                textOutsideEngine = ""
                DasherSessionCoordinator.resetOutputText(localHost)
            }

            val voice = VoiceInput(this, object : VoiceInput.Listener {
                override fun onVoiceModeChanged(mode: VoiceMode?) {
                    hostViews.showVoiceMode(mode)
                    val showPaused = DasherSessionCoordinator.isPaused() && mode != VoiceMode.LETTERS
                    canvasView.showPauseOverlay = showPaused
                    canvasView.invalidate()
                    pausedView.visibility = if (showPaused) View.VISIBLE else View.GONE
                    if (mode != VoiceMode.LETTERS) {
                        DasherSessionCoordinator.cancelSteering(localHost)
                    }
                }

                override fun onVoiceHeard(mode: VoiceMode, alternatives: List<String>, language: DasherLanguage) {
                    if (!isResumed) return
                    when (mode) {
                        VoiceMode.DICTATION -> appendDictation(localHost, alternatives.first())
                        VoiceMode.LETTERS -> DasherSessionCoordinator.steer(
                            localHost,
                            SpokenLetters.parse(alternatives, language)
                        )
                    }
                }

                override fun onVoicePermissionNeeded() {
                    VoicePermissionActivity.open(this@MainActivity)
                }

                override fun onVoiceUnavailable() {
                    Toast.makeText(this@MainActivity, R.string.voice_unavailable, Toast.LENGTH_SHORT).show()
                }
            })
            voiceInput = voice

            fun toggleVoice(mode: VoiceMode) {
                if (voice.mode == mode) {
                    voice.stop()
                } else {
                    voice.start(mode, VoiceInput.languageFor(DasherPrefs.getLanguages(this)))
                }
            }
            hostViews.dictationButton.setOnClickListener { toggleVoice(VoiceMode.DICTATION) }
            hostViews.lettersButton.setOnClickListener { toggleVoice(VoiceMode.LETTERS) }

            modeSwitch.setOnCheckedChangeListener { _, checked ->
                if (suppressModeSwitchCallback) {
                    return@setOnCheckedChangeListener
                }
                // User made an explicit choice, do not apply deferred startup restore anymore.
                pendingStartupTiltRestore = false
                if (!tiltAvailable && checked) {
                    suppressModeSwitchCallback = true
                    modeSwitch.isChecked = false
                    suppressModeSwitchCallback = false
                    return@setOnCheckedChangeListener
                }
                val mode = if (checked) InputMode.TILT else InputMode.TOUCH
                val switched = DasherSessionCoordinator.setInputMode(localHost, mode)
                if (!switched) {
                    val actualChecked = DasherSessionCoordinator.getInputMode() == InputMode.TILT
                    if (modeSwitch.isChecked != actualChecked) {
                        suppressModeSwitchCallback = true
                        modeSwitch.isChecked = actualChecked
                        suppressModeSwitchCallback = false
                    }
                    return@setOnCheckedChangeListener
                }
                DasherPrefs.setInputMode(this, mode)
                if (mode == InputMode.TILT) {
                    hostHandle?.let { DasherSessionCoordinator.requestPause(it) }
                }
                calibrateButton.isEnabled = DasherSessionCoordinator.getInputMode() == InputMode.TILT
                if (!checked) {
                    tiltProvider?.unregister()
                    DasherSessionCoordinator.clearTiltInput(localHost)
                } else {
                    tiltProvider?.calibrate()
                }
            }

            calibrateButton.setOnClickListener {
                tiltProvider?.calibrate()
            }

            if (canvasView.width > 0 && canvasView.height > 0) {
                DasherSessionCoordinator.onSurfaceSizeChanged(localHost, canvasView.width, canvasView.height)
            }
        } else {
            Log.e("DasherMobile", "Failed to initialize native engine")
            modeSwitch.isEnabled = false
            calibrateButton.isEnabled = false
        }
    }

    override fun onResume() {
        super.onResume()
        isResumed = true
        val localHost = hostHandle
        updateKeepScreenOn(DasherSessionCoordinator.getInputMode())
        if (localHost != null) {
            // Languages are chosen in the settings; a changed choice takes a fresh engine,
            // which starts with an empty text.
            if (DasherSessionCoordinator.activateHost(localHost, languagesPref())) {
                textOutsideEngine = ""
                outputView?.text = engineText
            }

            val prefModel = DasherPrefs.getLanguageModel(this)
            if (prefModel != DasherSessionCoordinator.getLanguageModel()) {
                DasherSessionCoordinator.setLanguageModel(localHost, prefModel)
            }
            val prefSpeed = DasherPrefs.getMovementSpeedPercent(this)
            if (prefSpeed != DasherSessionCoordinator.getMovementSpeedPercent()) {
                DasherSessionCoordinator.setMovementSpeedPercent(localHost, prefSpeed)
            }
            if (!startupModeApplied) {
                DasherSessionCoordinator.setInputMode(localHost, InputMode.TOUCH)
                startupModeApplied = true
            }
            if (pendingStartupTiltRestore) {
                scheduleStartupTiltRestore()
            } else {
                val prefMode = DasherPrefs.getInputMode(this)
                if (prefMode != DasherSessionCoordinator.getInputMode()) {
                    DasherSessionCoordinator.setInputMode(localHost, prefMode)
                }
            }

            if (DasherSessionCoordinator.getInputMode() == InputMode.TILT && DasherSessionCoordinator.isPaused()) {
                tiltProvider?.calibrate()
                DasherSessionCoordinator.unpause(localHost)
            }
        }
        if (DasherSessionCoordinator.getInputMode() == InputMode.TILT && !DasherSessionCoordinator.isPaused()) {
            tiltProvider?.register()
        }
    }

    override fun onPause() {
        voiceInput?.stop()
        tiltProvider?.unregister()
        hostHandle?.let {
            DasherSessionCoordinator.clearTiltInput(it)
            DasherSessionCoordinator.deactivateHost(it)
        }
        isResumed = false
        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        super.onPause()
    }

    override fun onDestroy() {
        voiceInput?.destroy()
        voiceInput = null
        tiltProvider?.unregister()
        tiltProvider = null
        hostHandle?.let { DasherSessionCoordinator.unregisterHost(it) }
        hostHandle = null
        super.onDestroy()
    }

    private fun languagesPref(): String = DasherLanguage.formatSet(DasherPrefs.getLanguages(this))

    /**
     * Appends dictated text to the text written so far, after a space if it follows a word,
     * and has Dasher continue behind it.  Its words count for the personal dictionary like
     * written ones.
     *
     * The engine only takes in text it could have written itself; of text with other characters
     * (digits, say) it keeps the part after the last of them.  The rest is kept in
     * [textOutsideEngine] and shown before the engine's text.
     */
    private fun appendDictation(localHost: DasherSessionCoordinator.HostHandle, text: String) {
        DasherSessionCoordinator.requestPause(localHost)
        val written = textOutsideEngine + engineText
        val separator = if (written.isNotEmpty() && !written.last().isWhitespace()) " " else ""
        val all = written + separator + text
        val loaded = DasherSessionCoordinator.setContext(localHost, all) ?: return
        textOutsideEngine = if (all.endsWith(loaded)) all.dropLast(loaded.length) else all
        NativeBridge.nativeDictionaryLearn(filesDir.absolutePath, text, DasherSessionCoordinator.getLanguages())
    }
}
