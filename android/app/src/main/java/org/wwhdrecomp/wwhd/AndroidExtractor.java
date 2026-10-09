package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.os.Build;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;
import org.json.JSONObject;

/** Verify our APK-installed extractor before passing its path to Python. */
public final class AndroidExtractor {
    private AndroidExtractor() {}

    public static JSONObject prepare(Context context) throws Exception {
        JSONObject metadata;
        try (InputStream stream = context.getAssets().open("extractor-apk.json")) {
            metadata = new JSONObject(new String(stream.readAllBytes(), StandardCharsets.UTF_8));
        }
        String abi = metadata.getString("abi");
        if (metadata.getInt("schema") != 1 || metadata.getInt("api") != 33 ||
            !Arrays.asList(Build.SUPPORTED_ABIS).contains(abi))
            throw new IOException("Unsupported extractor package");
        String name = "libwwhd-extract-driver.so";
        String expected = metadata.getString("sha256");
        if (!metadata.getString("executable").equals(name) || !expected.matches("[0-9a-f]{64}"))
            throw new IOException("Invalid extractor identity");
        File executable = new File(context.getApplicationInfo().nativeLibraryDir, name);
        if (!executable.isFile() || !executable.canExecute())
            throw new IOException("Extractor was not installed by Android");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(executable)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        StringBuilder actual = new StringBuilder();
        for (byte value : digest.digest()) actual.append(String.format("%02x", value & 255));
        if (!actual.toString().equals(expected)) throw new IOException("Extractor checksum mismatch");
        return new JSONObject().put("path", executable.getAbsolutePath())
            .put("identity", expected).put("abi", abi);
    }
}
