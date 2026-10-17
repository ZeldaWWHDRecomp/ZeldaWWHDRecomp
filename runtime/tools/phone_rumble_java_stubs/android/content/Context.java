package android.content;
public class Context {
 public android.os.VibratorManager manager = new android.os.VibratorManager();
 public Context getApplicationContext(){return this;}
 public <T> T getSystemService(Class<T> type){return type.cast(manager);}
 public ContentResolver getContentResolver(){return new ContentResolver();}
}
