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
HUD. Peak brightness should match your display's capability. Exposure and
highlight expansion adjust the game's HDR appearance; settings-menu brightness
is independent. HDR expands the game's SDR picture and cannot recover lighting
that was already clipped. Screenshots remain SDR.
