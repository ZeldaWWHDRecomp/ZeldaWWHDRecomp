package org.wwhdrecomp.wwhd;

import java.util.LinkedHashMap;
import java.util.Map;

/** What the game pickers ask Android's file picker for. Plain Java; SetupActivity turns it into the intent.
 *
 *  Android 11 and newer refuse the Download folder itself, the top of the internal storage and Android/data
 *  as a folder choice, so the folder picker opens in Download, where players put the game in a folder of
 *  its own and choose that folder. The advanced extras ask the picker to list the internal storage among its
 *  places (pickers that show it already ignore them). */
public final class SetupPicker {
    private SetupPicker() {}

    /** DocumentsContract.EXTRA_INITIAL_URI */
    public static final String EXTRA_INITIAL_URI = "android.provider.extra.INITIAL_URI";
    /** The picker's "show internal storage" switch, under its public and its older name. */
    public static final String EXTRA_SHOW_ADVANCED = "android.provider.extra.SHOW_ADVANCED";
    public static final String EXTRA_SHOW_ADVANCED_LEGACY = "android.content.extra.SHOW_ADVANCED";
    /** The internal storage's Download folder in Android's own storage provider. */
    public static final String STORAGE_AUTHORITY = "com.android.externalstorage.documents";
    public static final String DOWNLOAD_DOCUMENT = "primary:Download";
    public static final String DOWNLOAD_URI = "content://" + STORAGE_AUTHORITY + "/document/primary%3ADownload";

    /** The extras for a folder ({@code folder}) or a single-file pick: a String value is a content URI. */
    public static Map<String, Object> extras(boolean folder) {
        Map<String, Object> extras = new LinkedHashMap<>();
        extras.put(EXTRA_INITIAL_URI, DOWNLOAD_URI);
        extras.put(EXTRA_SHOW_ADVANCED, true);
        extras.put(EXTRA_SHOW_ADVANCED_LEGACY, true);
        return extras;
    }
}
