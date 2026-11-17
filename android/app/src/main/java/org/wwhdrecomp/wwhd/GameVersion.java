package org.wwhdrecomp.wwhd;

/** Setup's one choice of game version (docs/decomp-build.md "In setup (Android)"; the desktop's setup.py
 *  GAME_VERSIONS): the recompiled version (the default) or the decomp version, only the chosen one is built and started.
 *  "both" is an install an earlier app version built both for ("Also build the decomp version"): it keeps both and
 *  starts what it started until the player chooses. Plain Java, so the rules are testable without Android
 *  (GameVersionTest); AndroidGame keeps the files. */
public final class GameVersion {
    private GameVersion() {}

    public static final String RECOMP = "recomp", DECOMP = "decomp", BOTH = "both";

    /** The randomizer's refusal under the decomp choice (as the desktop's setup.RANDOMIZER_NEEDS_RECOMP). */
    public static final String RANDOMIZER_NEEDS_RECOMP = "Randomized games play in the recompiled version: choose " +
        "\"Recompiled version\" in setup to play one (the decomp version is chosen).";

    public static boolean known(String version) {
        return RECOMP.equals(version) || DECOMP.equals(version) || BOTH.equals(version);
    }

    /** The choice file (decomp-choice.json): {"version": "recomp"|"decomp"}; the earlier {"enabled", "start"}: enabled
     *  is "both" (kept until the player chooses), not enabled "recomp". version null: no "version" key. */
    public static String chosen(String version, boolean legacyEnabled) {
        if (RECOMP.equals(version) || DECOMP.equals(version)) return version;
        return legacyEnabled ? BOTH : RECOMP;
    }

    /** The game starts the decomp version: the decomp choice, or "both" with its earlier "start" choice. */
    public static boolean startsDecomp(String chosen, boolean legacyStart) {
        return DECOMP.equals(chosen) || (BOTH.equals(chosen) && legacyStart);
    }

    /** What a selection record (active/previous/normal.json) was built as: its "game_version", else (records written
     *  before the choice) "both" when it has the decomp version's keys. As setup_adapter.record_version. */
    public static String built(String gameVersion, boolean hasDecompLibrary, boolean hasDecompError) {
        if (known(gameVersion)) return gameVersion;
        return hasDecompLibrary || hasDecompError ? BOTH : RECOMP;
    }

    /** The active build does not match the choice: the chosen version is not what was built, or (both versions) the
     *  decomp version failed (a failed build is not a built one: setup tries again without a new choice). A randomized
     *  game is built by the recompiled version only. */
    public static boolean needsRebuild(String chosen, String built, boolean hasDecompLibrary, boolean randomized) {
        if (BOTH.equals(chosen)) {
            if (randomized) return DECOMP.equals(built);
            return !(BOTH.equals(built) && hasDecompLibrary);
        }
        return !chosen.equals(built);
    }

    /** A new active build of another version than the one before (a switch): the one before is not kept as the
     *  rollback, and its stage goes (the desktop removes the other version's game, its bin.previous copy too). */
    public static boolean switched(String previousBuilt, String newBuilt) {
        return !previousBuilt.equals(newBuilt);
    }

    /** The randomizer's refusal for the chosen version, or null (allowed). */
    public static String randomizerRefusal(String chosen) {
        return DECOMP.equals(chosen) ? RANDOMIZER_NEEDS_RECOMP : null;
    }
}
