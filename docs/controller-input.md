# Controller directions and Wind Waker controls

The physical controller layout and the emulated Wii U controller are separate
choices. Controls selects whether your inputs act as a Wii U GamePad or Pro
Controller. Select that controller in the game's Controller Selection screen too.
The face-button preset changes A/B/X/Y; it does not change D-pad or stick directions.

While conducting, the **right stick supplies the notes**, including down. The
**left stick controls the meter**: hold left or right to change the beat count.
The D-pad does not substitute for the note stick in the original game. For example,
the Song of Passing uses right, left, down on the right stick in three-beat meter.

D-pad up/down can move the Quest Log cursor in File Selection in both controller
modes. D-pad directions also navigate the pause-menu sidebar and item grid in
both modes. In GamePad mode, the inventory appears on the GamePad screen. Other
screens have their own controls; follow the game's stick/button prompts. The port does not turn D-pad presses into stick movement automatically.
If down fails, check its binding in Controls and test D-pad down, Left Stick Down
and Right Stick Down separately.

## Input diagnostics

For a reproducible controller report, these optional environment variables log
the packets written for the game:

- `WWHD_VPAD_TRACE=path`: GamePad reads.
- `WWHD_KPAD_TRACE=path`: Pro Controller reads on channel 0.

Each line contains `logic_step repeat hold trigger release lx ly rx ry`.
Button fields are hexadecimal; sticks are signed decimal values, with positive Y
pointing up. `repeat=1` identifies an interpolation/repeated input sample. The
existing four-column `WWHD_PAD_TRACE` format is unchanged.

D-pad down is `00000100` in VPAD and `00004000` in the Pro Controller's extended
button fields. In Pro Controller mode, the GamePad remains connected for its
screen and touch, but its buttons and sticks are idle. Read the KPAD trace for
controller input in that mode.

The game-free `sdl_input` test drives virtual Xbox and Switch Pro gamepads through
SDL polling and the configured bindings. `pad_delivery` checks the actual VPAD
and KPAD writers, including direction bits, press/release edges, both sticks,
interpolation repeats and fresh analog samples. Neither test reads game data or a save, or uses a physical controller. These
checks do not replace testing a particular physical device and its driver.
