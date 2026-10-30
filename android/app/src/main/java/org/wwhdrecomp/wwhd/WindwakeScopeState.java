// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

/** The native scope briefly publishes its item, then an empty message prompt instead of its controls. */
final class WindwakeScopeState {
    private int itemButton = -1;
    boolean active() { return itemButton >= 0; }
    int itemButton() { return itemButton; }

    void update(ContextPack pack) {
        if (pack.telescope()) {
            for (int b = ContextPack.X; b <= ContextPack.R; b++)
                if (pack.shown(b) && pack.kind(b) == ContextPack.KIND_ITEM && pack.label(b) == 0x20) {
                    itemButton = b; return;
                }
        }
        // Retain only this exact transition after native confirmation of the telescope.
        boolean emptyScopeMessage = pack.on() && (pack.flags() & ContextPack.TAP_CONTINUES) == 0 &&
            pack.shown(ContextPack.A) && pack.kind(ContextPack.A) == ContextPack.KIND_ACTION && pack.label(ContextPack.A) == 0;
        for (int b = ContextPack.B; b < ContextPack.COUNT; b++) emptyScopeMessage &= !pack.shown(b);
        if (!emptyScopeMessage) itemButton = -1;
    }
}
