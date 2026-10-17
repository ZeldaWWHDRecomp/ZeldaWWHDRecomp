package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

/** The saved Settings › Display › GamePad screen Off choice, read before the game starts (#222).
 * Native code reads settings.ini only once the game runs; a second display acquired before then
 * would be taken for a moment although the player switched it off. Same file and keys as
 * runtime/src/gfx/vulkan/overlay_sdl.cpp, with the env.txt overrides of runtime/src/main.cpp. */
final class SavedSecondScreen {
    private SavedSecondScreen() { }

    static boolean off(File externalFiles) {
        if (externalFiles == null) return false;
        File settings = settingsFile(externalFiles, env(read(new File(externalFiles, "env.txt"))));
        return settings != null && off(read(settings));
    }

    /** native path(): WWHD_SETTINGS, none for test runs, else <config dir>/settings.ini */
    static File settingsFile(File externalFiles, Map<String, String> env) {
        String explicit = env.get("WWHD_SETTINGS");
        if (explicit != null) return resolve(externalFiles, explicit);
        if (env.containsKey("WWHD_NO_HOST_INPUT")) return null;
        String user = env.get("WWHD_USER_DIR");
        if (user != null) return new File(resolve(externalFiles, user), "settings.ini");
        String xdg = env.get("XDG_CONFIG_HOME");
        if (xdg != null) return new File(resolve(externalFiles, xdg), "wwhd/settings.ini");
        return new File(AndroidUserDirectories.config(externalFiles), "settings.ini");
    }

    /** env.txt as main.cpp applies it: NAME=value lines, # comments, later lines win. */
    static Map<String, String> env(String text) {
        Map<String, String> values = new HashMap<>();
        if (text == null) return values;
        for (String line : text.split("\n", -1)) {
            int end = line.indexOf('\r');
            if (end >= 0) line = line.substring(0, end);
            int eq = line.indexOf('=');
            if (line.startsWith("#") || eq <= 0) continue;
            values.put(line.substring(0, eq), line.substring(eq + 1));
        }
        return values;
    }

    /** settings.ini: the last androidSecondScreenOff line wins, read like atof() != 0. */
    static boolean off(String settings) {
        if (settings == null) return false;
        String value = null;
        for (String line : settings.split("\n", -1)) {
            int end = line.indexOf('\r');
            if (end >= 0) line = line.substring(0, end);
            if (line.isEmpty() || line.startsWith("#")) continue;
            int eq = line.indexOf('=');
            if (eq >= 0 && line.substring(0, eq).equals("androidSecondScreenOff")) value = line.substring(eq + 1);
        }
        return value != null && number(value) != 0;
    }

    private static double number(String text) {
        String s = text.trim();
        int i = 0;
        if (i < s.length() && (s.charAt(i) == '+' || s.charAt(i) == '-')) i++;
        while (i < s.length() && (Character.isDigit(s.charAt(i)) || s.charAt(i) == '.')) i++;
        try { return Double.parseDouble(s.substring(0, i)); } catch (NumberFormatException e) { return 0; }
    }

    private static File resolve(File base, String path) {
        File file = new File(path);
        return file.isAbsolute() ? file : new File(base, path);  // native chdir()s to the external files folder
    }

    private static String read(File file) {
        try {
            return file.isFile() ? new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8) : null;
        } catch (IOException | SecurityException e) {
            return null;
        }
    }
}
