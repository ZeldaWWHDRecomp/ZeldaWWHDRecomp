package org.wwhdrecomp.wwhd;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** The game's action words and item icons for the context buttons (controls plan P3). Native code exports
 *  them once per game pack into the app cache (nativeContextAssets(dir): 0 not ready yet, -1 failed,
 *  > 0 the files' generation): dir/actions.txt ("HH<TAB>word" lines) and dir/item_HH.png. Nothing of the
 *  game ships with the app. This class decides when to ask native code again and reads actions.txt;
 *  TouchControls loads the files.
 *  Pure Java (no Android): tools/android/ContextButtonsTest.java. */
final class ContextAssets {
    static final long RETRY_MS = 1000;
    static final int MAX_WORD = 32, MAX_TEXT = 64 * 1024;
    static final String ACTIONS = "actions.txt";

    private int generation;   // the generation of the files loaded (0: none)
    private boolean failed;   // the export failed: letters and English words until the pack's generation changes
    private int failedAt;     // the pack generation seen when it failed
    private long nextAsk;

    int generation() { return generation; }
    boolean failed() { return failed; }

    /** Whether to call nativeContextAssets now. packGeneration: the pack's assets generation (out[2]). */
    boolean shouldAsk(long now, int packGeneration) {
        if (packGeneration > 0 && packGeneration != generation && (!failed || packGeneration != failedAt)) return true;
        if (generation > 0 || failed) return false;
        return now >= nextAsk;
    }

    /** nativeContextAssets returned r. True when the files of a new generation are to be loaded. */
    boolean result(int r, long now, int packGeneration) {
        if (r == 0) { nextAsk = now + RETRY_MS; return false; }
        if (r < 0) { failed = true; failedAt = packGeneration; return false; }
        failed = false;
        if (r == generation) return false;
        generation = r;
        return true;
    }

    /** The file name of an item's icon. */
    static String itemFile(int item) { return String.format(Locale.ROOT, "item_%02X.png", item & 0xFF); }

    /** actions.txt: "HH<TAB>word" per line (HH the action code in hex); bad lines are skipped, words are
     *  trimmed, control characters dropped, and cut to MAX_WORD characters. */
    static Map<Integer, String> parseActions(String text) {
        Map<Integer, String> out = new HashMap<>();
        if (text == null || text.length() > MAX_TEXT) return out;
        for (String line : text.split("\r?\n")) {
            final int tab = line.indexOf('\t');
            if (tab <= 0 || tab > 2) continue;
            final int code;
            try { code = Integer.parseInt(line.substring(0, tab), 16); } catch (NumberFormatException e) { continue; }
            StringBuilder w = new StringBuilder();
            for (char ch : line.substring(tab + 1).toCharArray()) if (!Character.isISOControl(ch)) w.append(ch);
            String word = w.toString().trim();
            if (word.length() > MAX_WORD) word = word.substring(0, MAX_WORD).trim();
            if (!word.isEmpty() && !out.containsKey(code)) out.put(code, word);
        }
        return out;
    }

    /** A word that is too long for one line at a readable size: split at the space nearest its middle
     *  (null: no space, it stays one line). */
    static String[] twoLines(String word) {
        if (word == null) return null;
        int best = -1;
        for (int i = word.indexOf(' '); i >= 0; i = word.indexOf(' ', i + 1))
            if (best < 0 || Math.abs(i * 2 - word.length()) < Math.abs(best * 2 - word.length())) best = i;
        if (best <= 0 || best >= word.length() - 1) return null;
        return new String[] {word.substring(0, best).trim(), word.substring(best + 1).trim()};
    }
}
