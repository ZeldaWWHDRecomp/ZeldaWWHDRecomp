#pragma once
#include <algorithm>
#include <cmath>

namespace gamepad_touch {
// Normalized viewport after the renderer applied rotation, fit and integer
// scaling. A drag may leave the image; an initial press must start inside it.
struct Viewport { float x=0, y=0, w=0, h=0; };
inline bool map(Viewport box, float x, float y, bool dragging, float& tx, float& ty) {
    if (!std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(box.x) || !std::isfinite(box.y) ||
        !std::isfinite(box.w) || !std::isfinite(box.h) || box.w <= 0 || box.h <= 0)
        return false;
    tx = (x - box.x) / box.w;
    ty = (y - box.y) / box.h;
    if (!dragging && (tx < 0 || tx > 1 || ty < 0 || ty > 1)) return false;
    tx = std::clamp(tx, 0.0f, 1.0f);
    ty = std::clamp(ty, 0.0f, 1.0f);
    return true;
}
}
