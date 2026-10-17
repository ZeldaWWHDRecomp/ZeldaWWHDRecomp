package org.wwhdrecomp.wwhd;

import java.util.ArrayList;
import java.util.List;

/** Checks a chosen game folder before anything is copied. The game folder holds code, content and meta at
 *  its top (with code/cking.rpx and meta/meta.xml). A folder that holds the game one or two levels down
 *  is used through that subfolder; a folder inside the game, an incomplete or different game, several
 *  games and no game at all are refused with a message that says which folder to choose. Plain Java:
 *  SetupImport lists the folders through Android's document provider. */
public final class SetupFolderCheck {
    private SetupFolderCheck() {}

    /** One child of a listed folder. */
    public static final class Entry {
        public final String name, id;
        public final boolean directory;
        public Entry(String name, String id, boolean directory) {
            this.name = name;
            this.id = id;
            this.directory = directory;
        }
    }

    /** Lists the children of a folder by its document id. */
    public interface Lister { List<Entry> list(String id) throws Exception; }

    public enum Kind { GAME, SUBFOLDER, INSIDE_GAME, NOT_THIS_GAME, SEVERAL, NO_GAME }

    public static final class Result {
        public final Kind kind;
        /** The folder to import (GAME, SUBFOLDER); null when the choice is refused. */
        public final String document;
        /** SUBFOLDER, NOT_THIS_GAME: the folder's path below the chosen one ("" for the chosen one itself);
         *  INSIDE_GAME: code, content or meta; SEVERAL: the game folders, comma-separated. */
        public final String path;
        Result(Kind kind, String document, String path) {
            this.kind = kind;
            this.document = document;
            this.path = path;
        }
        public boolean usable() { return kind == Kind.GAME || kind == Kind.SUBFOLDER; }

        /** English text for the setup record (the screen uses the translated strings). */
        public String message() {
            switch (kind) {
                case GAME: return "The chosen folder contains the game.";
                case SUBFOLDER: return "Using the game folder \"" + path + "\" inside the folder you chose.";
                case INSIDE_GAME: return "This is the game's " + path + " folder. Choose the folder one level up, " +
                    "the one that contains code, content and meta.";
                case NOT_THIS_GAME: return (path.isEmpty() ? "This folder" : "The folder \"" + path + "\"") +
                    " has code, content and meta, but not the files of The Wind Waker HD (code/cking.rpx and " +
                    "meta/meta.xml). Check that the game copy is complete.";
                case SEVERAL: return "This folder contains more than one game folder (" + path +
                    "). Choose the one you want.";
                default: return "This folder doesn't contain the game. Choose the folder that contains code, " +
                    "content and meta.";
            }
        }
    }

    /** How far below the chosen folder the game is looked for, and how many folders are opened at most. */
    static final int DEPTH = 2, MAX_FOLDERS = 200;
    private static final String[] PARTS = {"code", "content", "meta"};

    private static Entry child(List<Entry> entries, String name, boolean directory) {
        for (Entry entry : entries) if (entry.name.equals(name) && entry.directory == directory) return entry;
        return null;
    }

    /** null: not a game folder; GAME or NOT_THIS_GAME otherwise. */
    private static Kind game(Lister lister, List<Entry> entries) throws Exception {
        Entry code = child(entries, "code", true), content = child(entries, "content", true), meta = child(entries, "meta", true);
        if (code == null || content == null || meta == null) return null;
        boolean rpx = child(lister.list(code.id), "cking.rpx", false) != null;
        boolean xml = child(lister.list(meta.id), "meta.xml", false) != null;
        return rpx && xml ? Kind.GAME : Kind.NOT_THIS_GAME;
    }

    private static final class Folder {
        final String id, path;
        Folder(String id, String path) { this.id = id; this.path = path; }
    }

    /** Checks the chosen folder {@code root}; {@code rootName} is its display name, if known. */
    public static Result check(Lister lister, String root, String rootName) throws Exception {
        List<Entry> top = lister.list(root);
        Kind here = game(lister, top);
        if (here == Kind.GAME) return new Result(Kind.GAME, root, "");
        if (here == Kind.NOT_THIS_GAME) return new Result(Kind.NOT_THIS_GAME, null, "");
        // a folder inside the game: its code folder (cking.rpx), its meta folder (meta.xml) or one named so
        if (child(top, "cking.rpx", false) != null) return new Result(Kind.INSIDE_GAME, null, "code");
        if (child(top, "meta.xml", false) != null) return new Result(Kind.INSIDE_GAME, null, "meta");
        for (String part : PARTS) if (part.equals(rootName)) return new Result(Kind.INSIDE_GAME, null, part);
        // the game one or two levels down; the shallowest level with a game folder decides
        List<Folder> level = new ArrayList<>();
        for (Entry entry : top) if (searchable(entry)) level.add(new Folder(entry.id, entry.name));
        int opened = 1;
        for (int depth = 1; depth <= DEPTH && !level.isEmpty(); depth++) {
            List<Folder> games = new ArrayList<>(), others = new ArrayList<>(), next = new ArrayList<>();
            for (Folder folder : level) {
                if (opened >= MAX_FOLDERS) break;
                List<Entry> entries = lister.list(folder.id);
                opened++;
                Kind kind = game(lister, entries);
                if (kind == Kind.GAME) games.add(folder);
                else if (kind == Kind.NOT_THIS_GAME) others.add(folder);
                else if (depth < DEPTH)
                    for (Entry entry : entries) if (searchable(entry)) next.add(new Folder(entry.id, folder.path + "/" + entry.name));
            }
            if (games.size() == 1) return new Result(Kind.SUBFOLDER, games.get(0).id, games.get(0).path);
            if (games.size() > 1) {
                StringBuilder names = new StringBuilder();
                for (Folder folder : games) names.append(names.length() == 0 ? "" : ", ").append(folder.path);
                return new Result(Kind.SEVERAL, null, names.toString());
            }
            if (!others.isEmpty()) return new Result(Kind.NOT_THIS_GAME, null, others.get(0).path);
            level = next;
        }
        return new Result(Kind.NO_GAME, null, "");
    }

    /** Folders worth opening while looking for the game: not hidden, not a game's own parts. */
    private static boolean searchable(Entry entry) {
        if (!entry.directory || entry.name.startsWith(".")) return false;
        for (String part : PARTS) if (part.equals(entry.name)) return false;
        return true;
    }
}
