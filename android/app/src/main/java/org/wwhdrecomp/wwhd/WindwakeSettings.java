// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.LinkedHashMap;
import java.util.Map;

/** Real runtime keys from gfx/vulkan/overlay_sdl.cpp; no env overrides lock the in-game menu. */
final class WindwakeSettings {
    static final String[] NAMES = {"Poco · equilíbrio", "Economia", "Qualidade"};
    static final String[] DETAILS = {"30 FPS · 720p com resolução dinâmica até 540p",
        "30 FPS · resolução dinâmica até 360p · efeitos reduzidos", "30 FPS · 720p fixos · efeitos originais"};

    static Map<String, String> profile(int choice) {
        if (choice < 0 || choice >= NAMES.length) throw new IllegalArgumentException("Perfil inválido");
        Map<String, String> values = new LinkedHashMap<>();
        values.put("resScale", "1"); // runtime clamps this to >= 1; dynResMin is the downscaler
        values.put("dynResMin", choice == 0 ? "0.75" : choice == 1 ? "0.5" : "0");
        values.put("fps60", "0");
        values.put("interpFps", "60");
        values.put("fps60Paced", "1");
        values.put("heatGuard", "normal");
        values.put("aoMode", "2");
        values.put("aoHires", "0");
        values.put("aniso", choice == 2 ? "1" : "0");
        values.put("cloudShadows", choice == 2 ? "1" : "0");
        values.put("fullLight", "0");
        values.put("fxaa", choice == 1 ? "0" : "1");
        values.put("vkPresentMode", "0"); // FIFO, preserves frame pacing without tearing
        values.put("windwakeProfile", Integer.toString(choice));
        return values;
    }

    static Map<String, String> read(File file) throws IOException {
        Map<String, String> values = new LinkedHashMap<>();
        if (!file.isFile()) return values;
        if (file.length() > 1024 * 1024) throw new IOException("Arquivo de configurações grande demais.");
        for (String line : Files.readAllLines(file.toPath(), StandardCharsets.UTF_8)) {
            int eq = line.indexOf('=');
            if (eq > 0 && !line.startsWith("#")) values.put(line.substring(0, eq), line.substring(eq + 1));
        }
        return values;
    }

    static void defaults(File file, TransactionalSaveStore.Durability durability) throws IOException {
        Map<String, String> current = read(file);
        boolean changed = false;
        if (!current.containsKey("windwakeDefaults")) {
            // Only fill absent graphics keys: a migrated player's settings win.
            profile(0).forEach(current::putIfAbsent);
            current.putIfAbsent("drcMode", "auto");
            current.put("windwakeDefaults", "1");
            changed = true;
        }
        if (!"2".equals(current.get("windwakeTouchVersion"))) {
            // The requested phone controls replace this fork's former dual-stick defaults once.
            current.putAll(mobileControls());
            current.putIfAbsent("touchSwipeSpeed", "1.00");
            current.putIfAbsent("windwakeAimSpeed", "0.70");
            current.putIfAbsent("windwakeFixedMove", "0");
            current.put("windwakeTouchVersion", "2");
            changed = true;
        }
        if (!current.containsKey("itemButtonNames")) { current.put("itemButtonNames", "0"); changed = true; }
        if (changed) write(file, current, durability);
    }

    static Map<String, String> mobileControls() {
        Map<String, String> values = new LinkedHashMap<>();
        values.put("touchStyle", "1");
        values.put("touchRight", "0");
        values.put("touchStyleAsked", "1");
        values.put("touchShowSticks", "1");
        values.put("touchFollow", "0");
        values.put("touchControls", "1");
        values.put("touchButtons", "0");
        values.put("touchContextButtons", "1");
        values.put("touchItemTap", "1");
        values.put("touchAimOnly", "1");
        values.put("touchAutoPutAway", "1");
        values.put("touchZlLatch", "1");
        return values;
    }

    static void update(File file, Map<String, String> changes, TransactionalSaveStore.Durability durability) throws IOException {
        Map<String, String> current = read(file);
        current.putAll(changes);
        write(file, current, durability);
    }

    private static void write(File file, Map<String, String> values, TransactionalSaveStore.Durability durability) throws IOException {
        File parent = file.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Não foi possível criar as configurações.");
        File temporary = File.createTempFile("settings-", ".tmp", parent);
        try {
            StringBuilder text = new StringBuilder("# Windwake / Wind Waker HD\n");
            for (Map.Entry<String, String> value : values.entrySet()) text.append(value.getKey()).append('=').append(value.getValue()).append('\n');
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                output.write(text.toString().getBytes(StandardCharsets.UTF_8));
                output.getFD().sync();
            }
            Files.move(temporary.toPath(), file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            durability.sync(parent);
        } finally { temporary.delete(); }
    }
}
