// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.IOException;

/** Caller holds the app-wide SessionLease; native metadata selected the exact root first. */
final class GameSavePreparation {
    private GameSavePreparation() {}

    static void prepare(File root, File selected, TransactionalSaveStore.Durability durability) throws IOException {
        File target = selected.getCanonicalFile();
        File vanilla = new File(root, "save").getCanonicalFile();
        if (target.equals(vanilla)) {
            // Preserve interrupted-import recovery before creating an empty live directory.
            new TransactionalSaveStore(root, durability).prepareForGame();
            return;
        }
        // Imports/exports remain vanilla-only. A profile or explicit native root
        // never recovers, copies or moves the vanilla import transaction.
        if (target.isDirectory()) return;
        if (!target.mkdirs()) throw new IOException("Could not create the selected save directory.");
        File parent = target.getParentFile();
        if (parent != null) durability.sync(parent);
    }

    static void prepareLegacy(File root, TransactionalSaveStore.Durability durability) throws IOException {
        prepare(root, new File(root, "save"), durability);
    }
}
