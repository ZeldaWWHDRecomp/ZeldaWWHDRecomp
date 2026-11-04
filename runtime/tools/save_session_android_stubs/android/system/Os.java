package android.system;

import java.io.File;
import java.io.FileDescriptor;

/** Counts the Android durability boundary; does not claim to test Android fsync. */
public final class Os {
    public static int syncCalls;
    private Os() {}

    public static FileDescriptor open(String path, int flags, int mode) throws ErrnoException {
        if (!new File(path).isDirectory()) throw new ErrnoException("Not a directory");
        return new FileDescriptor();
    }

    public static void fsync(FileDescriptor descriptor) throws ErrnoException { syncCalls++; }
    public static void close(FileDescriptor descriptor) throws ErrnoException {}
}
