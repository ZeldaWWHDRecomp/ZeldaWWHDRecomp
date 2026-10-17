package org.wwhdrecomp.wwhd;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.Map;
import java.util.stream.Stream;

/** The saved GamePad screen Off choice is known before a second display is acquired (#222). */
public final class SavedSecondScreenTest {
    private static void check(boolean condition, String what) {
        if (!condition) throw new AssertionError(what);
    }

    private static void write(Path file, String text) throws Exception {
        Files.createDirectories(file.getParent());
        Files.write(file, text.getBytes(StandardCharsets.UTF_8));
    }

    public static void main(String[] args) throws Exception {
        // settings.ini as overlay_sdl.cpp writes and reads it
        check(!SavedSecondScreen.off((String) null), "no file: acquire");
        check(!SavedSecondScreen.off("# Wind Waker HD settings\ndrcMode=gamepad\n"), "never chosen: acquire");
        check(SavedSecondScreen.off("# c\ndrcMode=off\nandroidSecondScreenOff=1\n"), "saved Off");
        check(SavedSecondScreen.off("androidSecondScreenOff=1\r\n"), "CRLF");
        check(!SavedSecondScreen.off("androidSecondScreenOff=0\n"), "saved On");
        check(!SavedSecondScreen.off("androidSecondScreenOff=1\nandroidSecondScreenOff=0\n"), "last line wins");
        check(!SavedSecondScreen.off("#androidSecondScreenOff=1\n"), "comment");
        check(!SavedSecondScreen.off("androidSecondScreenOffX=1\nxandroidSecondScreenOff=1\n"), "exact key");
        check(!SavedSecondScreen.off("androidSecondScreenOff=\n"), "empty value");
        check(!SavedSecondScreen.off("androidSecondScreenOff=no\n"), "atof: not a number");
        check(SavedSecondScreen.off("androidSecondScreenOff= 2.5x\n"), "atof: leading number");

        // env.txt as main.cpp applies it
        Map<String, String> env = SavedSecondScreen.env("# c\nWWHD_A=1\r\n=x\nnoequals\nWWHD_A=2\nWWHD_B=a=b\n");
        check("2".equals(env.get("WWHD_A")) && "a=b".equals(env.get("WWHD_B")) && env.size() == 2, "env.txt parse");

        Path external = Files.createTempDirectory("wwhd-second-screen-");
        try {
            File root = external.toFile();
            check(SavedSecondScreen.settingsFile(root, Map.of()).equals(new File(root, "config/wwhd/settings.ini")), "default path");
            check(SavedSecondScreen.settingsFile(root, Map.of("WWHD_SETTINGS", "s.ini")).equals(new File(root, "s.ini")), "relative WWHD_SETTINGS");
            check(SavedSecondScreen.settingsFile(root, Map.of("WWHD_SETTINGS", "/x/s.ini")).equals(new File("/x/s.ini")), "absolute WWHD_SETTINGS");
            check(SavedSecondScreen.settingsFile(root, Map.of("WWHD_NO_HOST_INPUT", "1")) == null, "test runs: no settings");
            check(SavedSecondScreen.settingsFile(root, Map.of("WWHD_USER_DIR", "/u")).equals(new File("/u/settings.ini")), "WWHD_USER_DIR");
            check(SavedSecondScreen.settingsFile(root, Map.of("XDG_CONFIG_HOME", "/c")).equals(new File("/c/wwhd/settings.ini")), "XDG_CONFIG_HOME");

            check(!SavedSecondScreen.off(root), "fresh install: acquire");
            write(external.resolve("config/wwhd/settings.ini"), "drcMode=off\nandroidSecondScreenOff=1\n");
            check(SavedSecondScreen.off(root), "saved Off in the app folder");
            write(external.resolve("env.txt"), "WWHD_SETTINGS=other.ini\n");
            check(!SavedSecondScreen.off(root), "env.txt moves the settings file");
            write(external.resolve("other.ini"), "androidSecondScreenOff=1\n");
            check(SavedSecondScreen.off(root), "env.txt settings file read");
            check(!SavedSecondScreen.off((File) null), "no external storage");
        } finally {
            try (Stream<Path> paths = Files.walk(external)) {
                paths.sorted(Comparator.reverseOrder()).forEach(path -> path.toFile().delete());
            }
        }
        System.out.println("Saved GamePad screen Off: settings, env.txt, paths PASS");
    }
}
