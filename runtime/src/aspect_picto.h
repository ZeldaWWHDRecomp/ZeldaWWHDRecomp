// The Picto Box at other aspect ratios (issue #303): pure math shared by the guest hooks
// (aspect.cpp) and their test (runtime/tools/aspect_picto_test.cpp).
//
// At another aspect ratio the TV picture is the 16:9 picture widened (Hor+) or made taller (Vert+):
// its 16:9 part is 1/kx of its width and 1/ky of its height (kx, ky as aspect.cpp's factors: one
// of them is 1), centred unless the projection is off-centre (crop_to_16x9). The Picto Box's finder is a TV layout, which stays at its 16:9 place
// in the centre. The game takes the photo from the TV picture through a source rectangle in texture
// coordinates (dSnap_packet::draw: the finder's frame within the 1280x720 screen), and finds its
// subjects by drawing them into the photo's alpha channel with the TV projection narrowed to that
// frame (dSnap_packet::DebugDraw). Both assume a 16:9 TV picture. The hooks give the photo the same
// part of the scene as on a Wii U: the rectangle is moved into the 16:9 part of the wider picture,
// and the subject projection gets the 16:9 picture's scale back.
#pragma once

namespace aspect::picto {

// A source rectangle in texture coordinates of the 16:9 picture, as the game passes it to its
// texture copy (the copy reads at position + t * scale, t running over the destination, in each
// axis): the same rectangle in the k times wider (taller) picture that holds the 16:9 picture.
// shift: the projection's off-centre term along that axis, as texture coordinates grow (x: -m[0][2],
// y: +m[1][2] for a GX projection, v pointing down). The port widens (or heightens) the view by
// scaling m[0][0] (m[1][1]) only, which scales the picture about the projection's centre, not about
// the screen centre: with a shift (the Picto Box's camera has m[1][2] = 0.35, its finder sits above
// the screen centre) the 16:9 part is centred at 0.5 + shift / 2 * (1 - 1/k), not at 0.5.
// k = 1 leaves the values bit for bit.
inline void crop_to_16x9(float& position, float& scale, float k, float shift = 0.0f) {
    if (k == 1.0f) return;
    position = 0.5f + (position - 0.5f) / k + 0.5f * shift * (1.0f - 1.0f / k);
    scale /= k;
}

// The TV projection (row-major 4x4, rows x y z w) of a picture kx times wider and ky times taller
// than 16:9, narrowed to its 16:9 part: the 16:9 projection's x and y scale. Hor+ widened the
// picture by dividing m[0][0] by kx; Vert+ made it taller by dividing m[1][1] by ky (and m[0][0] with
// it, which the narrower aspect undid). The rest of the matrix (offsets, depth) is the same.
inline void narrow_to_16x9(float m[16], float kx, float ky) {
    if (kx != 1.0f) m[0] *= kx;
    if (ky != 1.0f) m[5] *= ky;
}

}  // namespace aspect::picto
