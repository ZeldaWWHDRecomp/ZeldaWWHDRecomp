import java.util.Arrays;
import org.wwhdrecomp.wwhd.FoldLayout;

public final class FoldLayoutTest {
    private static void expect(int[] expected, int[] actual) {
        if (!Arrays.equals(expected, actual))
            throw new AssertionError(Arrays.toString(actual));
    }
    public static void main(String[] args) {
        expect(new int[] {0,0,490,800,510,0,490,800},
            FoldLayout.panes(1000,800,0,0,490,0,510,800,true));
        expect(new int[] {0,0,1000,400,0,400,1000,400},
            FoldLayout.panes(1000,800,0,24,0,424,1000,424,false));
        expect(new int[] {0,0,480,800,500,0,500,800},
            FoldLayout.panes(1000,800,10,24,490,0,510,900,true));
        expect(null, FoldLayout.panes(0,800,0,0,490,0,510,800,true));
        expect(null, FoldLayout.panes(1000,800,0,0,0,0,0,800,true));
        expect(null, FoldLayout.panes(1000,800,0,0,990,0,1010,800,true));
        expect(null, FoldLayout.panes(1000,800,0,0,490,10,510,800,true));
        expect(null, FoldLayout.panes(1000,800,0,0,510,0,490,800,true));
        expect(null, FoldLayout.panes(1000,800,Integer.MIN_VALUE,0,
            Integer.MAX_VALUE,0,Integer.MAX_VALUE,800,true));
        System.out.println("Fold layout checks passed");
    }
}
