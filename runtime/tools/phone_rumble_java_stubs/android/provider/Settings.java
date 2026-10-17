package android.provider;
public class Settings { public static class System {
 public static final String HAPTIC_FEEDBACK_ENABLED="haptic_feedback_enabled";
 public static int enabled=1;
 public static int getInt(android.content.ContentResolver resolver,String key,int fallback){return enabled;}
}}
