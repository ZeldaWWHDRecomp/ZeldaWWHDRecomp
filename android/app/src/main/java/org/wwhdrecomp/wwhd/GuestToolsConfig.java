package org.wwhdrecomp.wwhd;

import java.util.ArrayList;
import java.util.List;

/** The code-mod build tools file (guest-sdk.json) for the phone: plain Java, no Android types.
 *
 *  The runtime reads the same file on every platform (runtime/src/mods/guest_build.h): the Python
 *  program and the compiler as argument vectors, the guest mod builder and the runtime headers. On
 *  the phone every path is absolute: the programs live in the app's native library folder (Android
 *  runs programs only from there), the builder and headers in the extracted Python sources. The app
 *  folder moves with every app update, so the game writes this file again in each session that
 *  uses code mods (AndroidGuestTools). */
final class GuestToolsConfig {
    private GuestToolsConfig() {}

    /** The guest module compiler: the setup's C compiler (driver, target, sysroot, resource folder),
     *  linking a shared library with the APK's lld; 16 KiB pages as every library of the app. */
    static List<String> compiler(List<String> cc, String lld) {
        if (cc.isEmpty()) throw new IllegalArgumentException("The compiler command is empty");
        requireAbsolute(cc.get(0));
        requireAbsolute(lld);
        List<String> command = new ArrayList<>(cc);
        command.add("-fuse-ld=lld");
        command.add("--ld-path=" + lld);
        command.add("-Wl,-z,max-page-size=16384");
        return command;
    }

    /** guest-sdk.json format 1 (absolute paths, no data or setup entry: the phone rebuilds the game
     *  code in its own setup). */
    static String json(String python, List<String> compiler, String builder, String include) {
        requireAbsolute(python);
        requireAbsolute(builder);
        requireAbsolute(include);
        StringBuilder out = new StringBuilder("{\"format_version\": 1, \"python\": [");
        out.append(quote(python)).append("], \"compiler\": [");
        for (int i = 0; i < compiler.size(); i++) out.append(i == 0 ? "" : ", ").append(quote(compiler.get(i)));
        out.append("], \"builder\": ").append(quote(builder));
        out.append(", \"include\": ").append(quote(include)).append("}\n");
        return out.toString();
    }

    private static void requireAbsolute(String path) {
        if (path == null || !path.startsWith("/")) throw new IllegalArgumentException("Not an absolute path: " + path);
    }

    static String quote(String value) {
        StringBuilder out = new StringBuilder("\"");
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c == 0) throw new IllegalArgumentException("NUL in a tool argument");
            if (c == '"' || c == '\\') out.append('\\').append(c);
            else if (c < 0x20) out.append(String.format("\\u%04x", (int) c));
            else out.append(c);
        }
        return out.append('"').toString();
    }
}
