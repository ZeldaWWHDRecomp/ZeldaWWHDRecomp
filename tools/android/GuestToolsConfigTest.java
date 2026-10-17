package org.wwhdrecomp.wwhd;

import java.util.Arrays;
import java.util.List;

/** javac -d out android/app/src/main/java/org/wwhdrecomp/wwhd/GuestToolsConfig.java tools/android/GuestToolsConfigTest.java
 *  java -cp out org.wwhdrecomp.wwhd.GuestToolsConfigTest [--print]
 *  --print writes one sample file to stdout (test_guest_tools_config.py reads it as the runtime does). */
public final class GuestToolsConfigTest {
    private static void expect(Object expected, Object actual) {
        if (!java.util.Objects.equals(expected, actual))
            throw new AssertionError("Expected " + expected + ", got " + actual);
    }

    private static void rejects(Runnable action) {
        try { action.run(); } catch (IllegalArgumentException expected) { return; }
        throw new AssertionError("accepted an invalid tool path");
    }

    static final String LIB = "/data/app/~~x==/org.wwhdrecomp.wwhd-y==/lib/arm64";
    static final String DATA = "/data/user/0/org.wwhdrecomp.wwhd/no_backup";

    static String sample() {
        List<String> cc = Arrays.asList(LIB + "/libwwhd-clang-driver.so", "--driver-mode=gcc",
            "--target=aarch64-linux-android33", "--sysroot=" + DATA + "/toolchains/id/sysroot",
            "-resource-dir=" + DATA + "/toolchains/id/lib/clang/20");
        List<String> compiler = GuestToolsConfig.compiler(cc, LIB + "/libwwhd-ld.lld-driver.so");
        return GuestToolsConfig.json(LIB + "/libwwhd-python-driver.so", compiler,
            DATA + "/embedded-python/r/source/tools/guestmod/build_guest_mod.py",
            DATA + "/embedded-python/r/source/sdk/include");
    }

    public static void main(String[] args) {
        if (args.length == 1 && args[0].equals("--print")) { System.out.print(sample()); return; }
        List<String> cc = Arrays.asList("/lib/clang", "--driver-mode=gcc");
        List<String> compiler = GuestToolsConfig.compiler(cc, "/lib/lld");
        expect(Arrays.asList("/lib/clang", "--driver-mode=gcc", "-fuse-ld=lld", "--ld-path=/lib/lld",
            "-Wl,-z,max-page-size=16384"), compiler);
        expect(Arrays.asList("/lib/clang", "--driver-mode=gcc"), cc);  // the setup's own command is not changed
        expect("{\"format_version\": 1, \"python\": [\"/lib/python\"], \"compiler\": [\"/lib/clang\", \"-x\"], " +
               "\"builder\": \"/src/build_guest_mod.py\", \"include\": \"/src/sdk/include\"}\n",
            GuestToolsConfig.json("/lib/python", Arrays.asList("/lib/clang", "-x"), "/src/build_guest_mod.py", "/src/sdk/include"));
        expect("\"a\\\"b\\\\c\\u000a\"", GuestToolsConfig.quote("a\"b\\c\n"));
        rejects(() -> GuestToolsConfig.quote("a\0b"));
        rejects(() -> GuestToolsConfig.compiler(Arrays.asList("clang"), "/lib/lld"));
        rejects(() -> GuestToolsConfig.compiler(cc, "ld.lld"));
        rejects(() -> GuestToolsConfig.compiler(Arrays.asList(), "/lib/lld"));
        rejects(() -> GuestToolsConfig.json("python3", compiler, "/b", "/i"));
        rejects(() -> GuestToolsConfig.json("/p", compiler, "build_guest_mod.py", "/i"));
        rejects(() -> GuestToolsConfig.json("/p", compiler, "/b", "sdk/include"));
        System.out.println("GuestToolsConfigTest passed");
    }
}
