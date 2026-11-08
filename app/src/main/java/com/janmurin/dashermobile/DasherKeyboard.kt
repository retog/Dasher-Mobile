package com.janmurin.dashermobile

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.provider.Settings
import android.view.inputmethod.InputMethodManager

/**
 * Dasher as the system keyboard ([DasherImeService]): whether it can be used, and the steps
 * towards using it.
 *
 * Android keeps a newly installed keyboard switched off: no keyboard switcher lists it until
 * the user turns it on in Android's keyboard settings, and it shows only once it is chosen as
 * the keyboard in use.  [setUp] walks the user through both steps.
 */
object DasherKeyboard {

    enum class State {
        /** Not turned on in Android's keyboard settings, so it cannot be chosen anywhere. */
        OFF,
        /** Turned on, but another keyboard is in use. */
        ON,
        /** The keyboard in use. */
        CURRENT
    }

    /** `true` while the user is in Android's keyboard settings, sent there by [setUp]. */
    private var chooseWhenBack = false

    fun state(context: Context): State {
        val imm = context.getSystemService(InputMethodManager::class.java) ?: return State.OFF
        if (imm.enabledInputMethodList.none { it.packageName == context.packageName }) {
            return State.OFF
        }
        val current = Settings.Secure.getString(
            context.contentResolver,
            Settings.Secure.DEFAULT_INPUT_METHOD
        )
        val currentPackage = current?.let { ComponentName.unflattenFromString(it)?.packageName }
        return if (currentPackage == context.packageName) State.CURRENT else State.ON
    }

    /**
     * Takes the next step towards typing with Dasher: while it is off, opens Android's keyboard
     * settings to turn it on (and offers the keyboard chooser on return, see [onWindowFocused]);
     * once it is on, opens the keyboard chooser.
     */
    fun setUp(activity: Activity) {
        if (state(activity) != State.OFF) {
            showChooser(activity)
            return
        }
        chooseWhenBack = true
        try {
            activity.startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS))
        } catch (e: ActivityNotFoundException) {
            activity.startActivity(Intent(Settings.ACTION_SETTINGS))
        }
    }

    /**
     * To be called when an activity's window gains focus.  Back from Android's keyboard settings
     * with Dasher turned on, the keyboard chooser opens, so that Dasher can be chosen right away.
     */
    fun onWindowFocused(activity: Activity) {
        if (!chooseWhenBack) return
        chooseWhenBack = false
        if (state(activity) == State.ON) {
            // Android shows the chooser only to the window it has given input focus; that
            // follows the window focus, so wait for it.
            activity.window.decorView.post { showChooser(activity) }
        }
    }

    /** Opens Android's keyboard chooser, which lists the keyboards turned on. */
    fun showChooser(context: Context) {
        context.getSystemService(InputMethodManager::class.java)?.showInputMethodPicker()
    }
}
