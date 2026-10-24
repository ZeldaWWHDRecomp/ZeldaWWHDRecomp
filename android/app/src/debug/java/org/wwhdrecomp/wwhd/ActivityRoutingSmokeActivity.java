package org.wwhdrecomp.wwhd;

/** Starts the real shader-preparation Activity; no mock of its routing hook. */
public final class ActivityRoutingSmokeActivity extends android.app.Activity {
    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        try {
            SetupStore.write(new java.io.File(getFilesDir(), "shader-routing-smoke.json"),
                new org.json.JSONObject().put("pending", true));
            startActivity(new android.content.Intent(this, ShaderPrepareActivity.class)
                .putExtra("result", new java.io.File(getCacheDir(), "routing-warm-result.json").getAbsolutePath()));
        } catch (Exception error) { throw new IllegalStateException(error); }
        finish();
    }
}
