package android.os;
public class Vibrator {
 public boolean present=true, amplitudeControl=true, denied=false; public int calls,cancels;
 public VibrationEffect effect; public VibrationAttributes attributes;
 public boolean hasVibrator(){return present;} public boolean hasAmplitudeControl(){return amplitudeControl;}
 public void vibrate(VibrationEffect effect,VibrationAttributes attributes){if(denied)throw new SecurityException();this.effect=effect;this.attributes=attributes;calls++;}
 public void cancel(){cancels++;}
}
