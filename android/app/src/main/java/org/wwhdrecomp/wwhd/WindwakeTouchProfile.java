// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.util.Map;

/** Windwake's phone layout and gesture rules. Coordinates are fractions of the safe view. */
final class WindwakeTouchProfile {
    static final String LAYOUT_KEY = "touch_layout_windwake_v2";
    static final int TEXT_RGB = 0xF8F4E8;
    final boolean fixedMovement;
    final int aimPercent;

    WindwakeTouchProfile(Map<String, String> settings) {
        fixedMovement = "1".equals(settings.get("windwakeFixedMove"));
        aimPercent = percent(settings.get("windwakeAimSpeed"), 70);
    }

    static int percent(String value, int fallback) {
        try {
            double n = Double.parseDouble(value);
            if (Double.isFinite(n)) return Math.max(25, Math.min(300, (int)Math.round(n * 100)));
        } catch (RuntimeException ignored) { }
        return fallback;
    }

    // Native already applies the camera speed. Divide it out so aim speed is independent.
    float swipeDelta(float pixels, boolean aiming, int cameraPercent) {
        if (!Float.isFinite(pixels)) return 0;
        return aiming ? pixels * aimPercent / Math.max(25f, Math.min(300f, cameraPercent)) : pixels;
    }

    // Preserve native ownership of dialogues, inventory and the GamePad picture.
    int freeRoute(int nativeRoute, boolean aiming) {
        if (nativeRoute != 1) return nativeRoute;
        return aiming ? 3 : fixedMovement ? 5 : 1; // swipe / consumed / movement stick
    }

    static boolean allowPinch(float firstX, float secondX, float width, boolean aiming) {
        return !aiming && Float.isFinite(firstX) && Float.isFinite(secondX) && width > 0 &&
            (firstX < width / 2) == (secondX < width / 2);
    }

    static TouchLayout.Placement home(String key) {
        switch (key) {
            case TouchLayout.KEY_LEFT_STICK: return p(.105f, .755f, .122f, 0, 90);
            case TouchLayout.KEY_A: return p(.883f, .744f, .092f, 0, 100);
            case TouchLayout.KEY_B: return p(.781f, .806f, .071f, 0, 100);
            case TouchLayout.KEY_X: return p(.933f, .563f, .070f, 0, 95);
            case TouchLayout.KEY_Y: return p(.821f, .574f, .070f, 0, 95);
            case TouchLayout.KEY_R: return p(.925f, .105f, .057f, 1, 95);
            case TouchLayout.KEY_ZR: return p(.925f, .243f, .057f, 1, 90);
            case TouchLayout.KEY_ZL: return p(.074f, .105f, .057f, 1, 90);
            case TouchLayout.KEY_L: return p(.185f, .105f, .040f, 1, 85);
            case TouchLayout.KEY_UP: return p(.11f, .345f, .030f, 1, 80);
            case TouchLayout.KEY_DOWN: return p(.11f, .465f, .030f, 1, 80);
            case TouchLayout.KEY_LEFT: return p(.072f, .405f, .030f, 1, 80);
            case TouchLayout.KEY_RIGHT: return p(.148f, .405f, .030f, 1, 80);
            case TouchLayout.KEY_PLUS: return p(.100f, .945f, .052f, 1, 90);
            case TouchLayout.KEY_LOOK: return p(.168f, .945f, .052f, 1, 90);
            case TouchLayout.KEY_MINUS: return p(.236f, .945f, .052f, 1, 90);
            case "10:0": return p(.304f, .945f, .052f, 1, 90);
            case "11:0": return p(.12f, .35f, .047f, 0, 95);
            case "11:1": return p(.12f, .49f, .047f, 0, 95);
            default: return null;
        }
    }
    private static TouchLayout.Placement p(float x, float y, float r, int shape, int opacity) {
        return new TouchLayout.Placement(x, y, r, shape, opacity, true);
    }

    static int fill(String key, boolean pressed, boolean filled, int opacity) {
        int rgb = 0x16181C;
        switch (key) {
            case TouchLayout.KEY_A: rgb = 0x226F7B; break;
            case TouchLayout.KEY_B: rgb = 0xA13D42; break;
            case TouchLayout.KEY_X: case TouchLayout.KEY_R: rgb = 0x537397; break;
            case TouchLayout.KEY_Y: rgb = 0x715B96; break;
            case TouchLayout.KEY_ZR: rgb = 0x76509E; break;
        }
        return color(pressed ? 205 : filled ? 125 : 0, rgb, opacity);
    }
    static int color(int alpha, int rgb, int opacity) {
        return Math.round(alpha * opacity / 100f) << 24 | rgb;
    }
}
