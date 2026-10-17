package android.os;
public class VibrationAttributes {
 public static final int USAGE_MEDIA=19; public int usage;
 public static class Builder {private int usage; public Builder setUsage(int value){usage=value;return this;}
 public VibrationAttributes build(){VibrationAttributes out=new VibrationAttributes();out.usage=usage;return out;}}
}
