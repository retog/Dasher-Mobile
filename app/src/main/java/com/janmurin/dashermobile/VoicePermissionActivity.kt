package com.janmurin.dashermobile

import android.Manifest
import android.content.Context
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts

/**
 * Asks for the permission to use the microphone, then closes.
 *
 * The keyboard is a service and cannot ask for a permission itself, so both hosts open this
 * activity when a voice mode is switched on without it.  It shows nothing of its own; the user
 * switches the voice mode on again once the permission is granted.
 */
class VoicePermissionActivity : ComponentActivity() {

    companion object {
        /** Opens the activity from an activity or a service. */
        fun open(context: Context) {
            context.startActivity(
                Intent(context, VoicePermissionActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        }
    }

    private val request = registerForActivityResult(ActivityResultContracts.RequestPermission()) {
        finish()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState == null) {
            request.launch(Manifest.permission.RECORD_AUDIO)
        }
    }
}
