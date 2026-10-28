package org.wwhdrecomp.wwhd;
public final class ControllerHintPolicyTest {
    public static void main(String[] args) {
        ControllerHintPolicy p = new ControllerHintPolicy();
        assert !p.claim("keyboard-with-dpad", false);   // not a game controller: never announced
        assert !p.claim(null, true) && !p.claim("", true);
        assert p.claim("pad-1", true);                  // first connection: native may offer the setup
        assert !p.claim("pad-1", true);                 // reconnect in this session: once
        assert p.claim("pad-2", true);                  // another controller: its own announcement
        assert !p.claim("pad-2", true);
        ControllerHintPolicy restarted = new ControllerHintPolicy();
        assert restarted.claim("pad-1", true);          // a new session announces again (native remembers the answer)
        System.out.println("Controller setup announcement: new controllers, reconnects, keyboards PASS");
    }
}
