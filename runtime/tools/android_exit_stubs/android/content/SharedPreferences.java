package android.content;
public interface SharedPreferences {
    long getLong(String key, long fallback);
    Editor edit();
    interface Editor { Editor putLong(String key, long value); boolean commit(); }
}
