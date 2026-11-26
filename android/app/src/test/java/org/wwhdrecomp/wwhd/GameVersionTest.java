package org.wwhdrecomp.wwhd;

// GameVersion (setup's choice of the recompiled or the decomp version): run with java -ea. The choice file, the legacy
// "both" install, what a record was built as, when a build needs rebuilding (a failed decomp build is not built), the
// switch that drops the other version, and the randomizer's refusal under the decomp choice.
public final class GameVersionTest {
    static void check(boolean condition, String what) {
        if (!condition) throw new AssertionError(what);
    }

    public static void main(String[] args) {
        // the choice file: {"version"}; no version: legacy {"enabled": true} = both, otherwise recompiled
        check(GameVersion.chosen("decomp", false).equals("decomp"), "decomp choice");
        check(GameVersion.chosen("recomp", true).equals("recomp"), "a version wins over legacy keys");
        check(GameVersion.chosen(null, true).equals("both"), "legacy enabled = both");
        check(GameVersion.chosen(null, false).equals("recomp"), "no choice = recompiled (the default)");
        check(GameVersion.chosen("both", false).equals("recomp"), "both is never written as a choice");
        check(GameVersion.chosen("garbage", false).equals("recomp"), "unknown = recompiled");

        // which one starts: the decomp choice starts it; legacy both keeps its "start"
        check(GameVersion.startsDecomp("decomp", false), "decomp starts decomp");
        check(!GameVersion.startsDecomp("recomp", true), "recompiled never starts decomp");
        check(GameVersion.startsDecomp("both", true) && !GameVersion.startsDecomp("both", false), "legacy start kept");

        // what a record was built as
        check(GameVersion.built("decomp", true, false).equals("decomp"), "new decomp record");
        check(GameVersion.built(null, false, false).equals("recomp"), "old record without decomp keys");
        check(GameVersion.built(null, true, false).equals("both"), "old record with a decomp library");
        check(GameVersion.built(null, false, true).equals("both"), "old record with a decomp error");

        // needs a rebuild: the chosen version against the built one
        check(!GameVersion.needsRebuild("recomp", "recomp", false, false), "recompiled built");
        check(GameVersion.needsRebuild("decomp", "recomp", false, false), "switch to decomp");
        check(GameVersion.needsRebuild("recomp", "decomp", true, false), "switch to recompiled");
        check(!GameVersion.needsRebuild("decomp", "decomp", true, false), "decomp built");
        check(GameVersion.needsRebuild("decomp", "recomp", false, true), "a randomized game is not the decomp version");
        check(GameVersion.needsRebuild("recomp", "both", true, false), "choosing on a both install keeps one");
        check(GameVersion.needsRebuild("decomp", "both", true, false), "choosing decomp on a both install keeps one");
        check(!GameVersion.needsRebuild("both", "both", true, false), "both built");
        check(GameVersion.needsRebuild("both", "both", false, false), "a failed decomp build is not built");
        check(!GameVersion.needsRebuild("both", "recomp", false, true), "both + randomized: the recompiled game only");

        // a switch drops the other version's rollback and stage
        check(GameVersion.switched("recomp", "decomp") && GameVersion.switched("both", "recomp"), "switches");
        check(!GameVersion.switched("decomp", "decomp"), "a rebuild of the same version is no switch");

        // the randomizer under the decomp choice
        check(GameVersion.randomizerRefusal("decomp").startsWith("Randomized games play in the recompiled version"),
              "refused under decomp");
        check(GameVersion.randomizerRefusal("recomp") == null && GameVersion.randomizerRefusal("both") == null,
              "allowed otherwise");
        System.out.println("GameVersionTest OK");
    }
}
