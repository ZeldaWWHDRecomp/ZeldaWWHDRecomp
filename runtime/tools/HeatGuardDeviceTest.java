package org.wwhdrecomp.wwhd;

public final class HeatGuardDeviceTest {
    private static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    private static boolean fan(String manufacturer, String brand, String model) {
        return HeatGuardDevice.classify(manufacturer, brand, model) == HeatGuardDevice.FAN_HANDHELD;
    }
    public static void main(String[] args) {
        // actively cooled handhelds
        check(fan("AYANEO", "AYANEO", "Pocket S2 Pro"), "AYANEO Pocket S2 Pro (issue #247)");
        check(fan("ayaneo", "", ""), "manufacturer, any case");
        check(fan("Unknown", "AYA", "Pocket S"), "brand AYA");
        check(fan("AYA", null, null), "manufacturer AYA, nulls");
        check(fan("", "", "AYANEO Pocket Air"), "model naming AYANEO");
        check(fan("KONKR", "KONKR", "Pocket FIT"), "KONKR Pocket FIT");
        check(fan("AYN", "AYN", "Odin2"), "AYN Odin 2");
        check(fan("AYN", "AYN", "AYN Thor"), "AYN Thor");
        check(fan("Qualcomm", "qti", "Odin2 Portal"), "Odin model on a generic build");
        check(fan("Retroid", "Retroid", "Retroid Pocket 5"), "Retroid Pocket 5");
        check(fan("Moorechip", "Moorechip", "Retroid Pocket Mini"), "Retroid model on another maker");
        check(fan(" AYN ", "", ""), "surrounding spaces");
        // phones and unknown devices keep the phone limits
        check(!fan("samsung", "samsung", "SM-S938B"), "Galaxy S25 Ultra");
        check(!fan("Google", "google", "Pixel 9 Pro"), "Pixel");
        check(!fan("Xiaomi", "Xiaomi", "Mi 11"), "Xiaomi");
        check(!fan("asus", "asus", "ROG Phone 9"), "phone with an optional clip-on fan: phone");
        check(!fan("Aynova", "Ayala", "Rayaneo X2"), "names that only contain the letters");
        check(!fan("AYANEOX", "", ""), "manufacturer must match exactly");
        check(!fan(null, null, null), "nothing known");
        check(!fan("", "", ""), "empty");
        check(HeatGuardDevice.reason("AYN", "AYN", "Odin2") != null, "reason for a handheld");
        check(HeatGuardDevice.reason("samsung", "samsung", "SM-S938B") == null, "no reason for a phone");
        System.out.println("heat guard device: ok");
    }
}
