package org.wwhdrecomp.wwhd;

import java.util.Map;

/** The game pickers start in Download and ask for the internal storage among the picker's places. */
public final class SetupPickerTest {
    private static void expect(Object expected, Object actual, String what) {
        if (!java.util.Objects.equals(expected, actual)) throw new AssertionError(what + ": expected " + expected + ", got " + actual);
    }

    public static void main(String[] args) {
        for (boolean folder : new boolean[] {true, false}) {
            Map<String, Object> extras = SetupPicker.extras(folder);
            expect(3, extras.size(), "extra count");
            // DocumentsContract.EXTRA_INITIAL_URI, pointing at Download in Android's storage provider
            expect("content://com.android.externalstorage.documents/document/primary%3ADownload",
                extras.get("android.provider.extra.INITIAL_URI"), "initial folder");
            expect(Boolean.TRUE, extras.get("android.provider.extra.SHOW_ADVANCED"), "show internal storage");
            expect(Boolean.TRUE, extras.get("android.content.extra.SHOW_ADVANCED"), "show internal storage (older name)");
        }
        // the URI is DocumentsContract.buildDocumentUri(authority, "primary:Download")
        expect("content://" + SetupPicker.STORAGE_AUTHORITY + "/document/" +
            SetupPicker.DOWNLOAD_DOCUMENT.replace(":", "%3A"), SetupPicker.DOWNLOAD_URI, "document URI encoding");
        System.out.println("SetupPickerTest passed");
    }
}
