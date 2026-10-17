package android.content;
import java.io.File;
public abstract class Context {
    public static final int MODE_PRIVATE = 0;
    public abstract File getFilesDir();
    public abstract String getPackageName();
    public abstract <T> T getSystemService(Class<T> type);
    public abstract SharedPreferences getSharedPreferences(String name, int mode);
}
