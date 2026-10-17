package org.wwhdrecomp.wwhd;
import java.util.Set;

public final class TouchLayoutTest {
    private static void check(boolean value,String message) { if (!value) throw new AssertionError(message); }
    private static void inside(TouchLayout.Placement p,int w,int h) {
        float r=p.radius*Math.min(w,h);
        check(p.x*w-r>=-.001f && p.x*w+r<=w+.001f && p.y*h-r>=-.001f && p.y*h+r<=h+.001f,"control clipped after fit");
    }
    public static void main(String[] args) {
        TouchLayout layout=new TouchLayout(Set.of("1:0","4:0","0:6"));
        check(layout.get("1:0")==null,"default layout must have no overrides");
        check(layout.put("1:0",-.8f,3f,20f),"finite edits accepted and bounded");
        TouchLayout.Placement p=layout.get("1:0");check(p.x==0 && p.y==1 && p.radius==TouchLayout.MAX_RADIUS,"bounds");
        for (int[] size:new int[][]{{1920,1080},{1080,1920},{320,240},{240,320},{3000,200}}) inside(TouchLayout.fit(p,size[0],size[1]),size[0],size[1]);
        // A toolbar can grow after rotation or font wrapping. All previews,
        // including large controls in a short landscape pane, remain below it.
        for (int[] view:new int[][]{{1920,1080,160},{1080,1920,240},{320,240,160},{240,320,180}}) {
            TouchLayout.Placement preview=TouchLayout.fit(p,view[0],view[1],view[2]);inside(preview,view[0],view[1]);
            check(preview.y*view[1]-preview.radius*Math.min(view[0],view[1])>=view[2]-.001f,"toolbar hides control");
        }
        TouchLayout copy=new TouchLayout(Set.of("1:0","4:0","0:6"));copy.decode(layout.encode());
        check(copy.encode().equals(layout.encode()),"persistent round trip");
        for (float bad:new float[]{Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY}) {
            String before=layout.encode();check(!layout.put("1:0",bad,.5f,.1f),"bad x");check(!layout.put("1:0",.5f,bad,.1f),"bad y");check(!layout.put("1:0",.5f,.5f,bad),"bad radius");check(before.equals(layout.encode()),"invalid edit mutates valid placement");
        }
        copy.decode("1;1:0=NaN,0.5,0.1;unknown=0.5,0.5,0.1;4:0=0.7,0.8,0.06;0:6=oops;");
        check(copy.get("1:0")==null && copy.get("unknown")==null && copy.get("4:0")!=null && copy.get("0:6")==null,"corrupt entry isolation");
        copy.decode("2;4:0=0.1,0.1,0.1;");check(copy.encode().equals("1;"),"unsupported schema");
        copy.decode("1;"+"x".repeat(4096));check(copy.encode().equals("1;"),"bounded stored input");
        layout.reset();check(layout.encode().equals("1;") && layout.get("1:0")==null,"reset restores exact defaults");
        TouchLayout.Placement tiny=TouchLayout.fit(new TouchLayout.Placement(0,0,.001f),1080,1920);inside(tiny,1080,1920);check(tiny.radius==TouchLayout.MIN_RADIUS,"minimum radius must be clamped before fitting");
        TouchLayout.EditSession edit=new TouchLayout.EditSession();int[] releases={0};
        edit.pointers(2);edit.setEditing(true,()->releases[0]++);check(edit.editing() && releases[0]==1,"enter releases held pad/touches");
        edit.setEditing(true,()->releases[0]++);check(releases[0]==1,"idempotent enter");
        edit.setEditing(false,()->releases[0]++);check(!edit.editing() && edit.discarding() && releases[0]==2,"exit releases and suppresses still-held gesture");
        edit.pointers(1);check(edit.discarding(),"secondary finger must not leak after exit");edit.pointers(0);check(!edit.discarding(),"new gesture allowed after all fingers released");
        edit.setEditing(true,()->releases[0]++);edit.setEditing(false,()->releases[0]++);check(!edit.discarding() && releases[0]==4,"no-finger transition permits immediate new input");
        System.out.println("Touch layout bounds, persistence, reset and edit transition checks passed");
    }
}
