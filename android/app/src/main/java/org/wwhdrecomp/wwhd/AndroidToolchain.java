package org.wwhdrecomp.wwhd;

import android.content.Context;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import org.json.JSONArray;
import org.json.JSONObject;

/** Verify installed compiler executables and prepare writable compiler data. */
public final class AndroidToolchain {
    private AndroidToolchain() {}

    public static synchronized JSONObject prepare(Context context) throws Exception {
        return prepare(context, () -> {});
    }

    static synchronized JSONObject prepare(Context context, EmbeddedPython.Control control) throws Exception {
        control.checkpoint();
        JSONObject metadata;
        try (InputStream stream = context.getAssets().open("toolchain-apk.json")) {
            metadata = new JSONObject(new String(stream.readAllBytes(), StandardCharsets.UTF_8));
        }
        String identity = metadata.getString("compiler_identity");
        if (!identity.matches("[0-9a-f]{64}")) throw new IOException("Invalid compiler identity");
        String abi = metadata.getString("abi");
        String triple;
        if (abi.equals("arm64-v8a")) triple = "aarch64-linux-android";
        else if (abi.equals("x86_64")) triple = "x86_64-linux-android";
        else throw new IOException("Unsupported compiler architecture");
        if (metadata.getInt("api") != 33) throw new IOException("Unsupported compiler API");
        String[] tools = {"clang", "ld.lld", "llvm-ar"};
        String[] names = {"libwwhd-clang-driver.so", "libwwhd-ld.lld-driver.so", "libwwhd-ar-driver.so"};
        File[] executables = new File[tools.length];
        for (int index = 0; index < tools.length; index++) {
            String name = names[index];
            if (!metadata.getJSONObject("executables").getString(tools[index]).equals(name))
                throw new IOException("Unexpected compiler executable mapping");
            File executable = new File(context.getApplicationInfo().nativeLibraryDir, name);
            if (!executable.isFile() || !executable.canExecute())
                throw new IOException("Compiler executable was not installed by Android");
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (InputStream input = new FileInputStream(executable)) {
                byte[] buffer = new byte[65536];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    control.checkpoint();
                    digest.update(buffer, 0, count);
                }
            }
            StringBuilder hash = new StringBuilder();
            for (byte value : digest.digest()) hash.append(String.format("%02x", value & 255));
            if (!hash.toString().equals(metadata.getJSONObject("sha256").getString(name)))
                throw new IOException("Installed compiler checksum mismatch");
            executables[index] = executable;
        }
        if (!EmbeddedPython.checksum(context, "toolchain-data.zip", control).equals(metadata.getString("data_sha256")))
            throw new IOException("Compiler data checksum mismatch");
        File parent = new File(context.getNoBackupFilesDir(), "toolchains");
        File data = new File(parent, identity);
        if (!ResourceVerifier.complete(data) ||
                !EmbeddedPython.resourcesMatch(context, "toolchain-data.zip", data, true, control)) {
            if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create compiler storage");
            File pending = new File(parent, identity + ".pending");
            EmbeddedPython.remove(pending);
            try {
                EmbeddedPython.extract(context, "toolchain-data.zip", pending, control);
                EmbeddedPython.publishResources(pending, data, control);
            } finally {
                EmbeddedPython.remove(pending);
            }
        }
        if (ResourceCacheCleanup.prune(parent, data, control::checkpoint)) EmbeddedPython.syncDirectory(parent);
        control.checkpoint();
        // Explicit paths are needed because APK executable names differ from
        // conventional bin/clang and bin/ld.lld. lld's installed name contains
        // an exact '-ld.lld-' component so its multicall driver selects ELF.
        JSONArray cc = new JSONArray().put(executables[0].getAbsolutePath())
            .put("--driver-mode=gcc").put("--target=" + triple + "33")
            .put("--sysroot=" + new File(data, "sysroot").getAbsolutePath())
            .put("-resource-dir=" + new File(data, "lib/clang/20").getAbsolutePath());
        JSONArray cxx = new JSONArray(cc.toString()).put("--driver-mode=g++")
            .put("-fuse-ld=lld").put("--ld-path=" + executables[1].getAbsolutePath());
        return new JSONObject().put("cc", cc).put("cxx", cxx)
            .put("ar", new JSONArray().put(executables[2].getAbsolutePath()))
            .put("identity", identity).put("data", data.getAbsolutePath()).put("abi", abi);
    }
}
