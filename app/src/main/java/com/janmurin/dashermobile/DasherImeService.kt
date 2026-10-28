package com.janmurin.dashermobile

import android.content.ComponentCallbacks2
import android.content.Context
import android.inputmethodservice.InputMethodService
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.util.Log
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.LinearLayout
import com.janmurin.dashermobile.ui.DasherHostUi
import com.janmurin.dashermobile.ui.DasherHostViews

/**
 * Android Input Method Service that presents the Dasher keyboard to host applications.
 *
 * ## Lifecycle overview
 * ```
 * onCreateInputView  → registers with DasherSessionCoordinator, wires callbacks
 * onStartInputView   → activates the host, turns learning off for password fields, loads
 *                      the text before the cursor, starts frame loop
 * onUpdateSelection  → when the editor reports a cursor Dasher's own edits do not explain (the
 *                      user moved it or selected text), pauses Dasher and loads the text
 *                      before it again
 * onFinishInputView  → resets text buffer, pauses + deactivates (engine and what its
 *                      language model learned are kept; learned text is saved)
 * onTrimMemory       → discards the native runtime while hidden if the system trims the
 *                      process as backgrounded (rare for the active keyboard)
 * onDestroy          → unregisters host, releases tilt sensor
 * ```
 *
 * ## Key responsibilities
 * - Creates the Dasher UI via [com.janmurin.dashermobile.ui.DasherHostUi] in compact IME mode.
 * - Routes rendered frames to [com.janmurin.dashermobile.ui.DasherCanvasView].
 * - Bridges DasherCore output text to the target editor via [DasherImeTextSink].
 * - Continues at the editor's cursor as if the text before it had been written with Dasher:
 *   on start, after the user moves the cursor or selects text, and from the locate button on
 *   the canvas.
 * - Manages the [TiltInputProvider] sensor registration in sync with visibility and mode.
 * - Persists and restores language, language-model and input-mode preferences via [DasherPrefs].
 */
class DasherImeService : InputMethodService() {

    companion object {
        private const val TAG = "DasherImeService"
        /** Characters before the cursor loaded into the engine; prediction needs only a few. */
        private const val MAX_CONTEXT_CHARS = 200
        /** Lets a moving cursor (e.g. a dragged selection handle) settle before it is checked. */
        private const val CURSOR_SYNC_DELAY_MS = 150L
        /** Dasher's own edits whose cursor the editor may still report. */
        private const val MAX_EXPECTED_CURSORS = 128
    }

    private var hostHandle: DasherSessionCoordinator.HostHandle? = null
    private var hostViews: DasherHostViews? = null
    private var imeTextSink: DasherImeTextSink? = null
    private var tiltProvider: TiltInputProvider? = null
    private var pendingStartupLanguage: DasherLanguage? = null
    private var suppressLanguageSwitchCallback = false
    private var suppressLanguageModelCallback = false
    private var suppressModeSwitchCallback = false
    private var shouldResetBufferOnNextStart = true
    private var isInputViewActive = false
    /** Editor text before the part loaded into the engine (cut at a character it lacks). */
    private var contextOutsideEngine = ""
    /**
     * The editor's selection as it last reported it, or as it was when input started; -1 while
     * unknown.  Reports keep arriving while the keyboard is hidden.
     */
    private var selectionStart = -1
    private var selectionEnd = -1
    /** The EditorInfo input last started with (see [onStartInput]). */
    private var startedEditorInfo: EditorInfo? = null
    /**
     * Where the editor's cursor was when its text last matched Dasher's (-1 while unknown), and
     * the length of the sink's [DasherImeTextSink.committedText] then.
     */
    private var syncedCursor = -1
    private var syncedCommittedLength = 0
    /** `true` if the text loaded last was cut at the window, so the editor holds more before it. */
    private var loadedWindowWasFull = false
    /**
     * Cursor positions Dasher's own edits lead to that the editor has not reported yet, oldest
     * first, so that a report of them, however late, is told apart from the user's.
     */
    private val expectedCursors = ArrayDeque<Int>()
    private val cursorSyncHandler = Handler(Looper.getMainLooper())
    private val cursorSyncRunnable = Runnable { checkCursor() }

    override fun onCreateInputView(): View {
        hostViews?.let { return it.root }

        val views = DasherHostUi.create(this, HostMode.IME)
        applyImeHeightSetting(views.root)
        hostViews = views
        imeTextSink = DasherImeTextSink(
            inputConnectionProvider = { currentInputConnection },
            editorInfoProvider = { currentInputEditorInfo },
            mayReadText = { mayCheckText(currentInputEditorInfo) }
        )
        pendingStartupLanguage = restoredLanguage()

        val localHost = DasherSessionCoordinator.registerHost(filesDir.absolutePath, assets)
        if (localHost == null) {
            Log.e(TAG, "Failed to initialize native engine for IME host")
            return views.root
        }
        hostHandle = localHost

        tiltProvider = TiltInputProvider(this) { nx, ny ->
            DasherSessionCoordinator.onTiltNormalized(localHost, nx, ny)
        }
        val tiltAvailable = tiltProvider?.hasSensor() == true
        if (!tiltAvailable) {
            views.modeSwitch?.isEnabled = false
        }

        DasherSessionCoordinator.updateHostCallbacks(
            host = localHost,
            frameConsumer = { commands, strings -> views.canvasView.submitFrame(commands, strings) },
            textConsumer = { text ->
                views.outputView.text = text
                val sink = imeTextSink
                when (sink?.onTextChanged(text)) {
                    DasherImeTextSink.Result.EDITED -> if (syncedCursor >= 0) {
                        expectedCursors.addLast(expectedCursor(sink))
                        if (expectedCursors.size > MAX_EXPECTED_CURSORS) expectedCursors.removeFirst()
                    }
                    // Dasher was about to delete text the editor does not hold before its cursor.
                    DasherImeTextSink.Result.OUT_OF_SYNC -> {
                        DasherSessionCoordinator.requestPause(localHost)
                        loadTextBeforeCursor()
                        // The editor may not have reported where its cursor now is; take the
                        // position from the next report its text confirms.
                        syncedCursor = -1
                    }
                    else -> Unit
                }
            },
            statusConsumer = { mode, paused ->
                val modeLabel = if (mode == InputMode.TILT) "TILT" else "TOUCH"
                val stateLabel = if (paused) "PAUSED" else "RUNNING"
                views.statusView.text = "$modeLabel | $stateLabel"
                views.languageSpinner?.isEnabled = paused
                views.languageModelSpinner?.isEnabled = paused
                views.locateButton?.visibility = if (paused) View.VISIBLE else View.GONE
                val showCanvasCalibrateButton = paused && mode == InputMode.TILT
                views.canvasCalibrateButton.visibility = if (showCanvasCalibrateButton) View.VISIBLE else View.GONE
                views.canvasCalibrateButton.isEnabled = showCanvasCalibrateButton
                views.canvasView.showPauseOverlay = paused
                
                val switchChecked = mode == InputMode.TILT
                if (views.modeSwitch?.isChecked != switchChecked) {
                    suppressModeSwitchCallback = true
                    views.modeSwitch?.isChecked = switchChecked
                    suppressModeSwitchCallback = false
                }
                views.calibrateButton?.isEnabled = switchChecked
                
                if (mode == InputMode.TILT && isInputViewActive) {
                    if (!paused) {
                        tiltProvider?.register()
                    } else {
                        tiltProvider?.unregister()
                    }
                } else {
                    tiltProvider?.unregister()
                }
            }
        )

        views.canvasView.onSurfaceSizeChanged = { width, height ->
            DasherSessionCoordinator.onSurfaceSizeChanged(localHost, width, height)
            applyPendingLanguage(localHost)
            // The engine can only load text once it has a surface (realised).
            loadTextBeforeCursor()
        }
        views.canvasView.onTouchInput = { action, x, y ->
            DasherSessionCoordinator.onTouch(localHost, action, x, y)
        }

        views.modeSwitch?.setOnCheckedChangeListener { _, checked ->
            if (suppressModeSwitchCallback) return@setOnCheckedChangeListener
            val mode = if (checked) InputMode.TILT else InputMode.TOUCH
            val switched = DasherSessionCoordinator.setInputMode(localHost, mode)
            if (switched) {
                DasherPrefs.setInputMode(this, mode)
            } else {
                val actualChecked = DasherSessionCoordinator.getInputMode() == InputMode.TILT
                if (views.modeSwitch?.isChecked != actualChecked) {
                    suppressModeSwitchCallback = true
                    views.modeSwitch?.isChecked = actualChecked
                    suppressModeSwitchCallback = false
                }
            }
        }

        views.calibrateButton?.setOnClickListener {
            tiltProvider?.calibrate()
            DasherSessionCoordinator.unpause(localHost)
        }

        views.locateButton?.setOnClickListener {
            loadTextBeforeCursor()
        }

        views.canvasCalibrateButton.setOnClickListener {
            tiltProvider?.calibrate()
            DasherSessionCoordinator.unpause(localHost)
        }

        setupLanguageControls(views, localHost)
        setupLanguageModelControls(views, localHost)
        applyImeStartupState(localHost)

        if (views.canvasView.width > 0 && views.canvasView.height > 0) {
            DasherSessionCoordinator.onSurfaceSizeChanged(localHost, views.canvasView.width, views.canvasView.height)
            applyPendingLanguage(localHost)
        }

        return views.root
    }

    override fun onStartInput(attribute: EditorInfo?, restarting: Boolean) {
        super.onStartInput(attribute, restarting)
        // After a configuration change the framework starts again with the same EditorInfo, whose
        // initial selection is out of date; the selection reported since then is current.
        if (attribute !== startedEditorInfo) {
            startedEditorInfo = attribute
            selectionStart = attribute?.initialSelStart ?: -1
            selectionEnd = attribute?.initialSelEnd ?: -1
        }
        pendingStartupLanguage = restoredLanguage()
        shouldResetBufferOnNextStart = true
        hostHandle?.let { applyImeStartupState(it) }
    }

    override fun onStartInputView(info: EditorInfo?, restarting: Boolean) {
        super.onStartInputView(info, restarting)
        val localHost = ensureHost() ?: return
        hostViews?.let { applyImeHeightSetting(it.root) }
        isInputViewActive = true
        DasherSessionCoordinator.activateHost(localHost)
        DasherSessionCoordinator.setLearningEnabled(localHost, allowsLearning(info))
        applyImeStartupState(localHost)
        
        DasherSessionCoordinator.requestPause(localHost)
        imeTextSink?.resetTracking()
        contextOutsideEngine = ""
        hostViews?.outputView?.text = ""
        DasherSessionCoordinator.resetOutputText(localHost)
        loadTextBeforeCursor()
        
        if (DasherSessionCoordinator.getInputMode() == InputMode.TILT && !DasherSessionCoordinator.isPaused()) {
            tiltProvider?.register()
        }
    }

    override fun onFinishInputView(finishingInput: Boolean) {
        isInputViewActive = false
        cursorSyncHandler.removeCallbacks(cursorSyncRunnable)
        tiltProvider?.unregister()
        hostHandle?.let {
            DasherSessionCoordinator.clearTiltInput(it)
            DasherSessionCoordinator.requestPause(it)
            // The engine outlives this editor: empty both the engine text and the sink's copy
            // so activating for the next editor never replays or deletes this editor's text.
            DasherSessionCoordinator.resetOutputText(it)
            DasherSessionCoordinator.setLearningEnabled(it, true)
            DasherSessionCoordinator.deactivateHost(it)
        }
        imeTextSink?.resetTracking()
        contextOutsideEngine = ""
        syncedCursor = -1
        expectedCursors.clear()
        super.onFinishInputView(finishingInput)
    }

    override fun onFinishInput() {
        selectionStart = -1
        selectionEnd = -1
        startedEditorInfo = null
        super.onFinishInput()
    }

    override fun onUpdateSelection(
        oldSelStart: Int,
        oldSelEnd: Int,
        newSelStart: Int,
        newSelEnd: Int,
        candidatesStart: Int,
        candidatesEnd: Int
    ) {
        super.onUpdateSelection(oldSelStart, oldSelEnd, newSelStart, newSelEnd, candidatesStart, candidatesEnd)
        selectionStart = newSelStart
        selectionEnd = newSelEnd
        if (!isInputViewActive || newSelStart < 0 || isReportOfOwnEdits()) return
        // The user (or the app) moved the cursor or selected text.  Stop Dasher from writing and
        // deleting there at once; once the cursor settles, check its text and reload.
        if (syncedCursor >= 0) {
            hostHandle?.let { DasherSessionCoordinator.requestPause(it) }
        }
        cursorSyncHandler.removeCallbacks(cursorSyncRunnable)
        cursorSyncHandler.postDelayed(cursorSyncRunnable, CURSOR_SYNC_DELAY_MS)
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (!isInputViewActive && level >= ComponentCallbacks2.TRIM_MEMORY_BACKGROUND) {
            hostHandle?.let { DasherSessionCoordinator.discardHostRuntime(it) }
        }
    }

    override fun onDestroy() {
        cursorSyncHandler.removeCallbacks(cursorSyncRunnable)
        tiltProvider?.unregister()
        tiltProvider = null
        hostHandle?.let { DasherSessionCoordinator.unregisterHost(it) }
        hostHandle = null
        hostViews = null
        imeTextSink = null
        super.onDestroy()
    }

    private fun ensureHost(): DasherSessionCoordinator.HostHandle? {
        val existing = hostHandle
        if (existing != null) {
            return existing
        }
        onCreateInputView()
        return hostHandle
    }

    private fun applyImeStartupState(localHost: DasherSessionCoordinator.HostHandle) {
        DasherSessionCoordinator.requestPause(localHost)
        
        val prefMode = DasherPrefs.getInputMode(this)
        if (prefMode != DasherSessionCoordinator.getInputMode()) {
            DasherSessionCoordinator.setInputMode(localHost, prefMode)
        }
        
        val prefModel = DasherPrefs.getLanguageModel(this)
        if (prefModel != DasherSessionCoordinator.getLanguageModel()) {
            DasherSessionCoordinator.setLanguageModel(localHost, prefModel)
        }

        val prefSpeed = DasherPrefs.getMovementSpeedPercent(this)
        if (prefSpeed != DasherSessionCoordinator.getMovementSpeedPercent()) {
            DasherSessionCoordinator.setMovementSpeedPercent(localHost, prefSpeed)
        }
        
        syncLanguageSelection()
        syncLanguageModelSelection()


        applyPendingLanguage(localHost)
    }

    private fun applyPendingLanguage(localHost: DasherSessionCoordinator.HostHandle) {
        val startupLanguage = pendingStartupLanguage ?: return
        if (DasherSessionCoordinator.setLanguage(localHost, startupLanguage)) {
            pendingStartupLanguage = null
            DasherPrefs.setLanguage(this, startupLanguage)
            syncLanguageSelection()
        }
    }

    private fun setupLanguageControls(views: DasherHostViews, localHost: DasherSessionCoordinator.HostHandle) {
        val languages = listOf(DasherLanguage.ENGLISH, DasherLanguage.SLOVAK)
        val adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            languages.map {
                if (it == DasherLanguage.SLOVAK) getString(R.string.language_slovak) else getString(R.string.language_english)
            }
        ).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        views.languageSpinner?.adapter = adapter
        syncLanguageSelection()

        views.languageSpinner?.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (suppressLanguageSwitchCallback) return
                val selectedLanguage = languages[position]
                val switched = DasherSessionCoordinator.setLanguage(localHost, selectedLanguage)
                if (!switched) {
                    syncLanguageSelection()
                    return
                }
                pendingStartupLanguage = null
                DasherPrefs.setLanguage(this@DasherImeService, selectedLanguage)
                // The switch emptied the engine text; reload the text before the cursor.
                loadTextBeforeCursor()
            }

            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
        }
    }

    private fun setupLanguageModelControls(views: DasherHostViews, localHost: DasherSessionCoordinator.HostHandle) {
        val models = LanguageModel.selectable
        val adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            models.map {
                when (it) {
                    LanguageModel.PPM -> getString(R.string.language_model_ppm)
                    LanguageModel.WORD -> getString(R.string.language_model_word)
                    LanguageModel.KENLM -> getString(R.string.language_model_kenlm)
                }
            }
        ).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        views.languageModelSpinner?.adapter = adapter
        syncLanguageModelSelection()

        views.languageModelSpinner?.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (suppressLanguageModelCallback) return
                val selectedModel = models[position]
                if (!DasherSessionCoordinator.setLanguageModel(localHost, selectedModel)) {
                    syncLanguageModelSelection()
                    return
                }
                DasherPrefs.setLanguageModel(this@DasherImeService, selectedModel)
            }

            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
        }
    }

    private fun syncLanguageSelection() {
        val views = hostViews ?: return
        val selection = if (DasherSessionCoordinator.getLanguage() == DasherLanguage.SLOVAK) 1 else 0
        suppressLanguageSwitchCallback = true
        views.languageSpinner?.setSelection(selection, false)
        suppressLanguageSwitchCallback = false
    }

    private fun syncLanguageModelSelection() {
        val views = hostViews ?: return
        val models = LanguageModel.selectable
        val selection = models.indexOf(DasherSessionCoordinator.getLanguageModel()).coerceAtLeast(0)
        suppressLanguageModelCallback = true
        views.languageModelSpinner?.setSelection(selection, false)
        suppressLanguageModelCallback = false
    }

    /**
     * Returns `true` if the editor's reported selection is where Dasher's own edits put it: at
     * the cursor of an edit not reported yet (the expectations up to it are dropped, as the
     * editor reports in order), or where all of them leave it.  A selection counts by its lower
     * end, where Dasher's next edit goes, so text an editor selects ahead of the cursor (a
     * browser's address bar completing a URL) is no reason to stop.
     */
    private fun isReportOfOwnEdits(): Boolean {
        val sink = imeTextSink ?: return false
        val index = expectedCursors.indexOf(reportedCursor())
        if (index >= 0) {
            repeat(index + 1) { expectedCursors.removeFirst() }
            return true
        }
        return isCursorWhereDasherLeftIt(sink)
    }

    /**
     * Loads the text before the cursor again if the editor's cursor is not where Dasher left it,
     * because the user moved it or selected text.  Text before the cursor that is what Dasher
     * believes confirms a cursor whose position alone was not conclusive.  In password fields the
     * text is never read, and a moved cursor starts an empty text.
     * Editors that report no positions are never checked; the locate button reloads there.
     */
    private fun checkCursor() {
        val sink = imeTextSink ?: return
        if (!isInputViewActive || selectionStart < 0 || isCursorWhereDasherLeftIt(sink)) return
        if (mayCheckText(currentInputEditorInfo) && textBeforeCursorMatches(sink)) {
            rememberCursor(sink)
            return
        }
        hostHandle?.let { DasherSessionCoordinator.requestPause(it) }
        loadTextBeforeCursor()
    }

    /** The lower end of the editor's selection (where Dasher's edits go), or -1 if unknown. */
    private fun reportedCursor(): Int =
        if (selectionStart < 0 || selectionEnd < 0) -1 else minOf(selectionStart, selectionEnd)

    /** Where Dasher's own edits since the text last matched have put the editor's cursor. */
    private fun expectedCursor(sink: DasherImeTextSink): Int =
        syncedCursor + sink.committedText.length - syncedCommittedLength

    /** Returns `true` if the editor reports its cursor where Dasher's own edits have put it. */
    private fun isCursorWhereDasherLeftIt(sink: DasherImeTextSink): Boolean =
        syncedCursor >= 0 && reportedCursor() == expectedCursor(sink)

    /** Records that the editor's text before its cursor now matches Dasher's. */
    private fun rememberCursor(sink: DasherImeTextSink) {
        syncedCursor = reportedCursor()
        syncedCommittedLength = sink.committedText.length
    }

    /** Returns `true` if the editor's text before its cursor is what Dasher believes it is. */
    private fun textBeforeCursorMatches(sink: DasherImeTextSink): Boolean {
        val raw = textBeforeCursor()
        val before = DasherImeTextSink.asTyped(raw.takeLast(MAX_CONTEXT_CHARS))
        val expected = (contextOutsideEngine + sink.committedText).takeLast(MAX_CONTEXT_CHARS)
        // Dasher does not know the editor's text before the window it was loaded from.
        val partial = loadedWindowWasFull || raw.length > MAX_CONTEXT_CHARS
        return if (partial) {
            before.endsWith(expected, ignoreCase = true)
        } else {
            before.equals(expected, ignoreCase = true)
        }
    }

    /**
     * Returns up to one character more than [MAX_CONTEXT_CHARS] before the cursor (or before a
     * selection), or "" if the editor gives none.
     */
    private fun textBeforeCursor(): String =
        currentInputConnection?.getTextBeforeCursor(MAX_CONTEXT_CHARS + 1, 0)?.toString().orEmpty()

    /**
     * Loads the text before the cursor into the engine, so Dasher continues exactly as if that
     * text had been written with it, and makes it the sink's baseline.
     *
     * Runs only while the keyboard is shown and Dasher is paused.  Nothing is loaded in editors
     * that must not be learned from; their text is still read where [mayCheckText] allows, so
     * the sink can check what Dasher deletes.
     */
    private fun loadTextBeforeCursor() {
        val localHost = hostHandle ?: return
        val sink = imeTextSink ?: return
        if (!isInputViewActive || !DasherSessionCoordinator.isPaused()) return
        val info = currentInputEditorInfo
        val learning = allowsLearning(info)
        val raw = if (mayCheckText(info)) textBeforeCursor() else ""
        val before = DasherImeTextSink.asTyped(raw.takeLast(MAX_CONTEXT_CHARS))
        // The engine keeps only the text after the last character it cannot read back, such as
        // '\r'.  Cut there too at line breaks the sink would not delete in a single-line editor,
        // and at a "\r\n" whose '\r' the window left out.
        var text = when {
            !learning -> ""
            sink.acceptsNewlines -> before
            else -> before.replace('\n', '\r')
        }
        if (learning && raw.length > MAX_CONTEXT_CHARS && raw.startsWith("\r\n")) {
            text = "\r$text"
        }
        val loaded = DasherSessionCoordinator.setContext(localHost, text) ?: return
        contextOutsideEngine = if (before.endsWith(loaded)) before.dropLast(loaded.length) else before
        loadedWindowWasFull = raw.length > MAX_CONTEXT_CHARS
        sink.resetTracking(loaded, contextOutsideEngine)
        rememberCursor(sink)
        expectedCursors.clear()
        hostViews?.outputView?.text = loaded
    }

    /**
     * Returns `false` for editors whose text must not be learned or saved: passwords, and
     * editors that ask for no personalised learning (e.g. incognito browser tabs).
     */
    private fun allowsLearning(info: EditorInfo?): Boolean {
        if (info == null) return true
        if ((info.imeOptions and EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING) != 0) return false
        return !isPassword(info)
    }

    /**
     * Returns `true` if the editor's text may be read to check what Dasher deletes.  That is no
     * learning, so it is allowed where only learning is not, but never in password fields, and
     * not in editors that hold no text (TYPE_NULL, e.g. terminals), which would return none.
     */
    private fun mayCheckText(info: EditorInfo?): Boolean {
        if (info == null) return true
        if ((info.inputType and InputType.TYPE_MASK_CLASS) == InputType.TYPE_NULL) return false
        return !isPassword(info)
    }

    private fun isPassword(info: EditorInfo): Boolean {
        val variation = info.inputType and InputType.TYPE_MASK_VARIATION
        return when (info.inputType and InputType.TYPE_MASK_CLASS) {
            InputType.TYPE_CLASS_TEXT -> variation == InputType.TYPE_TEXT_VARIATION_PASSWORD ||
                variation == InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD ||
                variation == InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD
            InputType.TYPE_CLASS_NUMBER -> variation == InputType.TYPE_NUMBER_VARIATION_PASSWORD
            else -> false
        }
    }

    private fun restoredLanguage(): DasherLanguage {
        return DasherPrefs.getLanguage(this)
    }

    private fun applyImeHeightSetting(rootView: View) {
        val percent = DasherPrefs.getImeHeightPercent(this)
        val screenHeight = resources.displayMetrics.heightPixels
        if (screenHeight <= 0) return

        val targetHeight = (screenHeight * (percent / 100f)).toInt()
        val layoutParams = rootView.layoutParams
        if (layoutParams != null) {
            layoutParams.width = ViewGroup.LayoutParams.MATCH_PARENT
            layoutParams.height = targetHeight
            rootView.layoutParams = layoutParams
        } else {
            rootView.layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                targetHeight
            )
        }
        rootView.minimumHeight = targetHeight
        rootView.requestLayout()
    }
}
