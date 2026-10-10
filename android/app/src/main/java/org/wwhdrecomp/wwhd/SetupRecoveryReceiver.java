package org.wwhdrecomp.wwhd;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import java.io.File;
import org.json.JSONObject;

/** Resume an interrupted setup or rebuild its retained inputs after an update. */
public final class SetupRecoveryReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if (!Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction()) &&
            !Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction())) return;
        PendingResult pending = goAsync();
        new Thread(() -> {
            try {
                File job = SetupStore.current(context);
                if (job == null || new File(job, "manual-pause.json").exists()) return;
                File host = new File(job, "host.json");
                JSONObject status = host.isFile() ? SetupStore.read(host) : new JSONObject();
                String state = status.optString("state");
                boolean update = Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction());
                boolean changed = update && state.equals("complete") && AndroidGame.needsRebuild(context);
                if (SetupRecoveryPolicy.shouldStart(state, false, update, changed))
                    SetupService.start(context, null);
            } catch (Exception failure) {
                Log.w("wwhd-setup", "Setup will resume when opened", failure);
            } finally { pending.finish(); }
        }, "wwhd-setup-recovery").start();
    }
}
