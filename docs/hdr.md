# HDR output

HDR output is available with Vulkan on Windows and Linux. Enable HDR for your
display in Windows or your Linux compositor, then choose HDR in Graphics settings.

**SDR curve in HDR** controls how the game's picture is turned into HDR. The
default, **Gamma 2.2**, matches most monitors in SDR. Try **Gamma 2.4** for darker
shadows and a TV-like look. **sRGB** restores the previous appearance. This setting
does not affect SDR output. The settings menu keeps its own brightness control.

**HDR format** chooses the signal sent to the display. **Auto** is best for most
setups: it uses scRGB when available, otherwise HDR10 (PQ). You can also choose
**scRGB** or **HDR10 (PQ)** to compare them. If your choice is unavailable, the game
uses the other HDR format, or SDR if neither is available. Graphics settings show
the active format and explain a fallback. Both settings take effect while playing.

Paper white controls the brightness of the game's normal whites, including its
HUD. On KDE Plasma, start with paper white 203 nits to match the desktop's SDR
brightness. KWin scales this reference by its SDR brightness setting.
Exposure adjusts overall brightness; settings-menu brightness is independent.

**Automatic peak brightness** uses paper white × SDL HDR headroom. It follows
changes to paper white and the window's display/headroom without restarting.
For example, paper white 203 and headroom 5 give Peak 1015; on the reported KDE
setup this maps to the physical display peak of 1150 nits. Valid headroom must
be finite and greater than 1. If unavailable, Auto uses 1025 nits and shows a
notice. Auto caps its result at 10000 nits, the HDR signal's absolute limit.
Headroom reporting depends on the driver/compositor; use manual Peak if the
reported result does not suit your setup.

Manual Peak is the default for compatibility across compositors. Auto is opt-in:
it has worked on the reported KDE setup, but on Sway it can report an unsuitable
10000-nit Peak. Explicitly saved Auto choices are preserved. Disabling Auto
restores your manual slider value. For manual Peak on KDE, use display peak × 203 / desktop SDR brightness
(all in nits), rather than the physical display peak alone. Other setups may
use the display's peak directly.

The game sends HDR metadata when the driver supports VK_EXT_hdr_metadata and
updates it when the settings change. Max CLL estimates the brightest mapped SDR
white or menu white, not unused Peak headroom; mastering maximum stays at Peak.

**Highlight expansion is where the HDR effect comes from.** The game renders an
SDR picture: at 0, HDR aims to reproduce SDR rather than add brighter highlights.
For an SDR comparison, use Gamma 2.2 and exposure 0. Raising highlight expansion
makes the sun and other bright areas brighter than normal white, limited by Peak.
Start around 0.15; try 0.30 for a stronger effect. HDR cannot recover lighting that
was already clipped. Screenshots remain SDR. Compositor screenshots (for example
on KDE) are tone mapped, so judge the result on the display itself.
