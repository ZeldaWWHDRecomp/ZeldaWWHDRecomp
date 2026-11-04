package android.content;

import java.io.File;

/** Only the storage lookup used by SaveSession; no Activity lifecycle is simulated. */
public abstract class Context {
    public abstract File getExternalFilesDir(String type);
}
