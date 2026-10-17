package android.os;
public class VibrationEffect {
 public static final int DEFAULT_AMPLITUDE=-1; public long duration; public int amplitude;
 public static VibrationEffect createOneShot(long ms,int value){VibrationEffect e=new VibrationEffect();e.duration=ms;e.amplitude=value;return e;}
}
