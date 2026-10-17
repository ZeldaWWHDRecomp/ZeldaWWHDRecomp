package org.wwhdrecomp.wwhd;
import android.content.Context;
import android.os.*;
import android.provider.Settings;
public final class PhoneRumbleTest {
 static void check(boolean condition){if(!condition)throw new AssertionError();}
 public static void main(String[] args){
  Context c=new Context(); Vibrator v=c.manager.vibrator; PhoneRumble p=new PhoneRumble(c);
  p.update(65535);check(v.calls==0); // never vibrate before resume
  p.setActive(true);p.update(32768);check(v.calls==1 && v.effect.amplitude==128);
  check(v.effect.duration==150 && v.attributes.usage==VibrationAttributes.USAGE_MEDIA);
  p.update(0);check(v.cancels==1);p.update(0);check(v.cancels==1);
  p.update(65535);p.setActive(false);check(v.cancels==2);
  p.update(65535);check(v.calls==2); // callbacks racing with pause are silenced
  p.setActive(true);p.update(65535);Settings.System.enabled=0;p.update(65535);
  check(v.cancels==3 && v.calls==3); // global system switch stops an existing lease
  Settings.System.enabled=1;v.amplitudeControl=false;p.update(1);
  check(v.effect.amplitude==VibrationEffect.DEFAULT_AMPLITUDE);
  v.denied=true;p.update(65535);check(v.cancels==4); // optional API denied: no game crash
  Context noHardware=new Context();noHardware.manager.vibrator.present=false;
  PhoneRumble absent=new PhoneRumble(noHardware);absent.setActive(true);absent.update(65535);
  check(noHardware.manager.vibrator.calls==0);
  Context noService=new Context();noService.manager=null;
  PhoneRumble missing=new PhoneRumble(noService);missing.setActive(true);missing.update(65535);
 }
}
