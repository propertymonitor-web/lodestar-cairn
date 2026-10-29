// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

import android.app.Application

/** The links to the radio and the repeater belong to the app, not to the screen, so turning the tablet or reopening the screen does not drop them. */
class CairnApp : Application() {
    lateinit var hub: Hub
        private set

    override fun onCreate() {
        super.onCreate()
        hub = Hub(this)
        hub.start()
    }
}
