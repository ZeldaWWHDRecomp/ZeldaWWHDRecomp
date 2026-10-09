package org.wwhdrecomp.wwhd;

/** Game-free test entry point, excluded from release APKs. */
public final class DisplaySmokeActivity extends WwhdActivity {
    private String syntheticPosture;

    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        if (state != null) syntheticPosture = state.getString("smoke_posture");
        try {
            SetupStore.write(new java.io.File(getFilesDir(), "display-smoke-activity.json"),
                    new org.json.JSONObject().put("instance", System.nanoTime())
                    .put("pid", android.os.Process.myPid()).put("reused", isReusingNativeSession())
                    .put("expected_thread", state == null ? 0 : state.getInt("smoke_thread"))
                    .put("native_thread", System.identityHashCode(mSDLThread)));
        } catch (Exception failure) { throw new IllegalStateException(failure); }
    }

    @Override protected void onSaveInstanceState(android.os.Bundle state) {
        state.putString("smoke_posture", syntheticPosture);
        state.putInt("smoke_thread", System.identityHashCode(mSDLThread));
        super.onSaveInstanceState(state);
    }
    private final android.content.BroadcastReceiver postureReceiver = new android.content.BroadcastReceiver() {
        @Override public void onReceive(android.content.Context context, android.content.Intent intent) {
            if (intent.getBooleanExtra("stale_surface_recovery", false)) {
                if (gamePadDisplay != null) gamePadDisplay.syntheticStaleRecovery();
                return;
            }
            if (intent.getBooleanExtra("dismiss_secondary", false)) {
                if (gamePadDisplay != null) gamePadDisplay.syntheticDismiss();
                return;
            }
            if (intent.getBooleanExtra("recreate", false)) {
                recreate();
                return;
            }
            if (intent.hasExtra("orientation")) {
                setRequestedOrientation(intent.getIntExtra("orientation",
                        android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE));
                return;
            }
            String posture = intent.getStringExtra("synthetic_fold");
            if (posture == null) return;
            syntheticPosture = posture;
            applySyntheticPosture();
        }
    };

    @Override protected void onResume() {
        if (gamePadDisplay == null && !mBrokenLibraries) {
            gamePadDisplay = new GamePadDisplay(this, mLayout, mSurface);
            gamePadDisplay.syntheticRejectShows(getIntent().getIntExtra("invalid_display_shows", 0));
        }
        super.onResume();
        registerReceiver(postureReceiver, new android.content.IntentFilter(
                getPackageName() + ".SMOKE_FOLD"), android.content.Context.RECEIVER_EXPORTED);
        if (syntheticPosture != null) mLayout.post(this::applySyntheticPosture);
    }

    @Override protected void onPause() {
        unregisterReceiver(postureReceiver);
        super.onPause();
    }

    private void applySyntheticPosture() {
        String fold = syntheticPosture;
        if (gamePadDisplay == null) return;
        int[] location = new int[2];
        mLayout.getLocationInWindow(location);
        int x = location[0], y = location[1], w = mLayout.getWidth(), h = mLayout.getHeight();
        android.graphics.Rect bounds = null;
        boolean vertical = "vertical".equals(fold);
        if (vertical) bounds = new android.graphics.Rect(x+w/2-10,y,x+w/2+10,y+h);
        if ("horizontal".equals(fold))
            bounds = new android.graphics.Rect(x,y+h/2-10,x+w,y+h/2+10);
        gamePadDisplay.syntheticFold(bounds, vertical);
    }

    @Override protected String[] getArguments() {
        String[] base = super.getArguments();
        boolean isolated = getIntent().getBooleanExtra("isolated_secondary", false);
        boolean general = getIntent().getBooleanExtra("shared_general", false);
        String[] arguments = java.util.Arrays.copyOf(base, base.length + 1 + (isolated ? 1 : 0) + (general ? 1 : 0));
        arguments[base.length] = "--display-smoke";
        int index = base.length + 1;
        if (isolated) arguments[index++] = "--isolated-secondary-smoke";
        if (general) arguments[index] = "--shared-general-smoke";
        return arguments;
    }
}
