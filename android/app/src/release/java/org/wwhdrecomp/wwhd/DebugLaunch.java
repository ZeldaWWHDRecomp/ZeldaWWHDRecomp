package org.wwhdrecomp.wwhd;

import android.app.Activity;

/** Release builds take no launch environment from intents (see the debug source set). */
final class DebugLaunch {
    private DebugLaunch() {}

    static void apply(Activity activity) {}
}
