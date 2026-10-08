// Right-to-left text: Arabic shaping and bidirectional order (rtl_text.h). Written from the Unicode
// data (ArabicShaping.txt joining types, the Arabic Presentation Forms blocks, UAX #9).
#include "rtl_text.h"

#include <algorithm>

namespace rtl {

namespace {

// Arabic letters U+0621..U+064A: joining type and the first presentation form (Forms-B, U+FE80..),
// in Unicode's order isolated, final[, initial, medial]. Letters without forms there have 0.
struct Letter {
    Joining join;
    uint16_t forms;  // first presentation form, 0 = none
};
constexpr Joining U = Joining::None, R = Joining::Right, D = Joining::Dual, C = Joining::Causing;
constexpr Letter kLetters[0x64A - 0x621 + 1] = {
    {U, 0xFE80},  // 0621 hamza (isolated only)
    {R, 0xFE81},  // 0622 alef with madda above
    {R, 0xFE83},  // 0623 alef with hamza above
    {R, 0xFE85},  // 0624 waw with hamza above
    {R, 0xFE87},  // 0625 alef with hamza below
    {D, 0xFE89},  // 0626 yeh with hamza above
    {R, 0xFE8D},  // 0627 alef
    {D, 0xFE8F},  // 0628 beh
    {R, 0xFE93},  // 0629 teh marbuta
    {D, 0xFE95},  // 062A teh
    {D, 0xFE99},  // 062B theh
    {D, 0xFE9D},  // 062C jeem
    {D, 0xFEA1},  // 062D hah
    {D, 0xFEA5},  // 062E khah
    {R, 0xFEA9},  // 062F dal
    {R, 0xFEAB},  // 0630 thal
    {R, 0xFEAD},  // 0631 reh
    {R, 0xFEAF},  // 0632 zain
    {D, 0xFEB1},  // 0633 seen
    {D, 0xFEB5},  // 0634 sheen
    {D, 0xFEB9},  // 0635 sad
    {D, 0xFEBD},  // 0636 dad
    {D, 0xFEC1},  // 0637 tah
    {D, 0xFEC5},  // 0638 zah
    {D, 0xFEC9},  // 0639 ain
    {D, 0xFECD},  // 063A ghain
    {D, 0},       // 063B keheh with two dots above
    {D, 0},       // 063C keheh with three dots below
    {D, 0},       // 063D farsi yeh with inverted v
    {D, 0},       // 063E farsi yeh with two dots above
    {D, 0},       // 063F farsi yeh with three dots above
    {C, 0},       // 0640 tatweel
    {D, 0xFED1},  // 0641 feh
    {D, 0xFED5},  // 0642 qaf
    {D, 0xFED9},  // 0643 kaf
    {D, 0xFEDD},  // 0644 lam
    {D, 0xFEE1},  // 0645 meem
    {D, 0xFEE5},  // 0646 noon
    {D, 0xFEE9},  // 0647 heh
    {R, 0xFEED},  // 0648 waw
    {D, 0xFEEF},  // 0649 alef maksura (initial/medial: U+FBE8/U+FBE9)
    {D, 0xFEF1},  // 064A yeh
};

// Letters outside U+0621..U+064A with forms in Presentation Forms-A (isolated, final[, initial, medial])
struct Extra {
    uint16_t cp;
    Joining join;
    uint16_t forms;
};
constexpr Extra kExtra[] = {
    {0x0671, R, 0xFB50},  // alef wasla
    {0x067E, D, 0xFB56},  // peh
    {0x0686, D, 0xFB7A},  // tcheh
    {0x0698, R, 0xFB8A},  // jeh
    {0x06A9, D, 0xFB8E},  // keheh
    {0x06AF, D, 0xFB92},  // gaf
    {0x06CC, D, 0xFBFC},  // farsi yeh
};

const Extra* extra(uint32_t cp) {
    for (const Extra& e : kExtra)
        if (e.cp == cp) return &e;
    return nullptr;
}

bool hebrew_letter(uint32_t cp) { return (cp >= 0x05D0 && cp <= 0x05EA) || (cp >= 0x05EF && cp <= 0x05F2); }

}  // namespace

Joining joining_type(uint32_t cp) {
    if (cp >= 0x0621 && cp <= 0x064A) return kLetters[cp - 0x0621].join;
    if (is_rtl_mark(cp) && cp >= 0x0600) return Joining::Transparent;
    if (const Extra* e = extra(cp)) return e->join;
    return Joining::None;
}

uint32_t presentation_form(uint32_t cp, Form form) {
    uint32_t first = 0;
    Joining j = Joining::None;
    if (cp >= 0x0621 && cp <= 0x064A) {
        first = kLetters[cp - 0x0621].forms;
        j = kLetters[cp - 0x0621].join;
        if (cp == 0x0649 && (form == Form::Initial || form == Form::Medial))
            return form == Form::Initial ? 0xFBE8 : 0xFBE9;
    } else if (const Extra* e = extra(cp)) {
        first = e->forms;
        j = e->join;
    }
    if (!first) return 0;
    const int n = j == Joining::Dual ? 4 : j == Joining::Right ? 2 : 1;
    const int k = (int)form;
    if (k >= n) return 0;
    return first + k;
}

uint32_t lam_alef(uint32_t alef, bool final_form) {
    uint32_t base;
    switch (alef) {
    case 0x0622: base = 0xFEF5; break;
    case 0x0623: base = 0xFEF7; break;
    case 0x0625: base = 0xFEF9; break;
    case 0x0627: base = 0xFEFB; break;
    default: return 0;
    }
    return base + (final_form ? 1 : 0);
}

bool is_rtl_mark(uint32_t cp) {
    return (cp >= 0x0591 && cp <= 0x05BD) || cp == 0x05BF || cp == 0x05C1 || cp == 0x05C2 || cp == 0x05C4 ||
           cp == 0x05C5 || cp == 0x05C7 || (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) ||
           cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) || cp == 0x06E7 ||
           cp == 0x06E8 || (cp >= 0x06EA && cp <= 0x06ED) || cp == 0xFB1E;
}

bool is_rtl_letter(uint32_t cp) {
    if (hebrew_letter(cp)) return true;
    if (cp >= 0xFB1D && cp <= 0xFB4F) return !is_rtl_mark(cp);
    if ((cp >= 0x0620 && cp <= 0x064A) || (cp >= 0x066E && cp <= 0x06D3) || cp == 0x06D5 ||
        (cp >= 0x06FA && cp <= 0x06FF))
        return cp != 0x0640 && !is_rtl_mark(cp);
    return (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFC);
}

Bidi bidi_class(uint32_t cp) {
    if (cp == 0x0A || cp == 0x0D || cp == 0x1C || cp == 0x1D || cp == 0x1E || cp == 0x85 || cp == 0x2029) return Bidi::B;
    if (cp == 0x09 || cp == 0x0B || cp == 0x1F) return Bidi::S;
    if (cp == 0x0C || cp == 0x20 || cp == 0x3000 || cp == 0x2028 || (cp >= 0x2000 && cp <= 0x200A)) return Bidi::WS;
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0) || cp == 0x200B || cp == 0x200C || cp == 0x200D ||
        cp == 0xFEFF)
        return Bidi::BN;
    if (cp >= '0' && cp <= '9') return Bidi::EN;
    if ((cp >= 0xFF10 && cp <= 0xFF19) || (cp >= 0x06F0 && cp <= 0x06F9) || cp == 0xB2 || cp == 0xB3 || cp == 0xB9)
        return Bidi::EN;
    if ((cp >= 0x0660 && cp <= 0x0669) || cp == 0x066B || cp == 0x066C) return Bidi::AN;
    if (cp == '+' || cp == '-' || cp == 0xFF0B || cp == 0xFF0D) return Bidi::ES;
    if (cp == '#' || cp == '$' || cp == '%' || cp == 0xA2 || cp == 0xA3 || cp == 0xA4 || cp == 0xA5 || cp == 0xB0 ||
        cp == 0xB1 || cp == 0x066A || cp == 0x20AC || cp == 0x2030 || cp == 0xFF03 || cp == 0xFF04 || cp == 0xFF05)
        return Bidi::ET;
    if (cp == ',' || cp == '.' || cp == '/' || cp == ':' || cp == 0xA0 || cp == 0x060C || cp == 0xFF0C ||
        cp == 0xFF0E || cp == 0xFF0F || cp == 0xFF1A)
        return Bidi::CS;
    if (is_rtl_mark(cp)) return Bidi::NSM;
    if (hebrew_letter(cp) || (cp >= 0xFB1D && cp <= 0xFB4F) || (cp >= 0x0590 && cp <= 0x05FF)) return Bidi::R;
    if ((cp >= 0x0600 && cp <= 0x07BF) || (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFE))
        return Bidi::AL;
    // ASCII punctuation and symbols, Latin-1 symbols, general punctuation, arrows, and the private use
    // area (the game fonts' button pictures): neutral, so they stay with the words around them
    if (cp < 0x80 && !((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))) return Bidi::ON;
    if ((cp >= 0xA1 && cp <= 0xBF) || cp == 0xD7 || cp == 0xF7) return Bidi::ON;
    if ((cp >= 0x2010 && cp <= 0x2027) || (cp >= 0x2030 && cp <= 0x2BFF) || (cp >= 0x3001 && cp <= 0x3003) ||
        (cp >= 0x3008 && cp <= 0x3011) || (cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0xFE50 && cp <= 0xFE6B) ||
        (cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) ||
        (cp >= 0xFF5B && cp <= 0xFF65) || cp == 0xFFFD)
        return Bidi::ON;
    return Bidi::L;
}

uint32_t mirror(uint32_t cp) {
    switch (cp) {
    case '(': return ')';
    case ')': return '(';
    case '<': return '>';
    case '>': return '<';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case 0xAB: return 0xBB;
    case 0xBB: return 0xAB;
    case 0x2039: return 0x203A;
    case 0x203A: return 0x2039;
    case 0x3008: return 0x3009;
    case 0x3009: return 0x3008;
    case 0x300A: return 0x300B;
    case 0x300B: return 0x300A;
    case 0x300C: return 0x300D;
    case 0x300D: return 0x300C;
    case 0x300E: return 0x300F;
    case 0x300F: return 0x300E;
    case 0x3010: return 0x3011;
    case 0x3011: return 0x3010;
    case 0xFF08: return 0xFF09;
    case 0xFF09: return 0xFF08;
    case 0xFF3B: return 0xFF3D;
    case 0xFF3D: return 0xFF3B;
    // the Wind Waker HD fonts' decorative brackets (private use area) around place names and credits
    case 0xE0A0: return 0xE0A1;
    case 0xE0A1: return 0xE0A0;
    }
    return cp;
}

// ---- shaping ----

namespace {

bool joins_left_side(Joining j) { return j == Joining::Dual || j == Joining::Causing; }   // to the next char
bool joins_right_side(Joining j) { return j == Joining::Dual || j == Joining::Right || j == Joining::Causing; }  // to the previous

// nearest non-transparent neighbour in direction step (-1 / +1), 0 if none
uint32_t neighbour(const std::function<uint32_t(size_t)>& at, size_t i, size_t n, int step) {
    for (size_t k = i;;) {
        if (step < 0) {
            if (k == 0) return 0;
            k--;
        } else {
            if (k + 1 >= n) return 0;
            k++;
        }
        const uint32_t c = at(k);
        if (joining_type(c) != Joining::Transparent) return c;
    }
}

}  // namespace

Shaped shape_at(const std::function<uint32_t(size_t)>& at, size_t i, size_t n, const HasGlyph& has) {
    const uint32_t c = at(i);
    if (is_rtl_mark(c)) return {c, !has(c)};
    const Joining j = joining_type(c);
    if (j == Joining::None || j == Joining::Transparent) return {c, false};
    const uint32_t prev = neighbour(at, i, n, -1), next = neighbour(at, i, n, +1);
    const bool join_prev = joins_right_side(j) && joins_left_side(joining_type(prev));
    // lam-alef: the ligature (when the font has it) replaces the lam; the alef is then skipped
    if (c == 0x0644 && lam_alef(next, false) && has(lam_alef(next, join_prev))) return {lam_alef(next, join_prev), false};
    if (lam_alef(c, false) && prev == 0x0644) {
        const uint32_t before_lam = [&] {
            // the character before the lam decides the ligature's form; find the lam first
            size_t k = i;
            while (k > 0 && at(k - 1) != 0x0644) k--;
            return k > 0 ? neighbour(at, k - 1, n, -1) : 0u;
        }();
        if (has(lam_alef(c, joins_left_side(joining_type(before_lam))))) return {c, true};
    }
    const bool join_next = joins_left_side(j) && joins_right_side(joining_type(next));
    const Form form = join_prev && join_next ? Form::Medial : join_prev ? Form::Final : join_next ? Form::Initial : Form::Isolated;
    // a missing form: the nearest one that makes sense, else the base letter
    const Form fallbacks[4][3] = {
        {Form::Isolated, Form::Isolated, Form::Isolated},
        {Form::Final, Form::Isolated, Form::Isolated},
        {Form::Initial, Form::Isolated, Form::Isolated},
        {Form::Medial, Form::Final, Form::Isolated},
    };
    for (Form f : fallbacks[(int)form]) {
        const uint32_t p = presentation_form(c, f);
        if (p && has(p)) return {p, false};
    }
    return {c, false};
}

std::vector<uint32_t> shape(const std::u16string& s, const HasGlyph& has) {
    std::vector<uint32_t> out;
    auto at = [&](size_t k) { return (uint32_t)s[k]; };
    for (size_t i = 0; i < s.size(); i++) {
        const Shaped r = shape_at(at, i, s.size(), has);
        if (!r.skip) out.push_back(r.code);
    }
    return out;
}

// ---- bidi ----

std::vector<uint8_t> resolve_levels(const std::vector<Bidi>& in, int para) {
    const size_t n = in.size();
    std::vector<Bidi> t = in;
    const Bidi sos = para & 1 ? Bidi::R : Bidi::L;
    // indices that take part (BN is removed, X9)
    std::vector<size_t> idx;
    for (size_t i = 0; i < n; i++)
        if (t[i] != Bidi::BN) idx.push_back(i);
    const size_t m = idx.size();
    auto T = [&](size_t k) -> Bidi& { return t[idx[k]]; };
    // W1: NSM takes the type of the previous character
    for (size_t k = 0; k < m; k++)
        if (T(k) == Bidi::NSM) T(k) = k ? T(k - 1) : sos;
    // W2: EN after AL becomes AN
    {
        Bidi strong = sos;
        for (size_t k = 0; k < m; k++) {
            if (T(k) == Bidi::L || T(k) == Bidi::R || T(k) == Bidi::AL) strong = T(k);
            else if (T(k) == Bidi::EN && strong == Bidi::AL) T(k) = Bidi::AN;
        }
    }
    // W3: AL -> R
    for (size_t k = 0; k < m; k++)
        if (T(k) == Bidi::AL) T(k) = Bidi::R;
    // W4: a single separator between two numbers of the same kind
    for (size_t k = 1; k + 1 < m; k++) {
        if (T(k) == Bidi::ES && T(k - 1) == Bidi::EN && T(k + 1) == Bidi::EN) T(k) = Bidi::EN;
        else if (T(k) == Bidi::CS && T(k - 1) == Bidi::EN && T(k + 1) == Bidi::EN) T(k) = Bidi::EN;
        else if (T(k) == Bidi::CS && T(k - 1) == Bidi::AN && T(k + 1) == Bidi::AN) T(k) = Bidi::AN;
    }
    // W5: terminators next to European numbers
    for (size_t k = 0; k < m;) {
        if (T(k) != Bidi::ET) {
            k++;
            continue;
        }
        size_t e = k;
        while (e < m && T(e) == Bidi::ET) e++;
        const bool en = (k > 0 && T(k - 1) == Bidi::EN) || (e < m && T(e) == Bidi::EN);
        if (en)
            for (size_t q = k; q < e; q++) T(q) = Bidi::EN;
        k = e;
    }
    // W6: remaining separators and terminators are neutral
    for (size_t k = 0; k < m; k++)
        if (T(k) == Bidi::ES || T(k) == Bidi::ET || T(k) == Bidi::CS) T(k) = Bidi::ON;
    // W7: EN after L (or an L start) becomes L
    {
        Bidi strong = sos;
        for (size_t k = 0; k < m; k++) {
            if (T(k) == Bidi::L || T(k) == Bidi::R) strong = T(k);
            else if (T(k) == Bidi::EN && strong == Bidi::L) T(k) = Bidi::L;
        }
    }
    // N1/N2: neutrals between strong types of the same direction take it (numbers count as R),
    // otherwise the embedding direction
    auto dir = [](Bidi b) { return b == Bidi::L ? Bidi::L : Bidi::R; };
    auto neutral = [](Bidi b) { return b == Bidi::WS || b == Bidi::ON || b == Bidi::S || b == Bidi::B; };
    for (size_t k = 0; k < m;) {
        if (!neutral(T(k))) {
            k++;
            continue;
        }
        size_t e = k;
        while (e < m && neutral(T(e))) e++;
        const Bidi before = k > 0 ? dir(T(k - 1)) : sos;
        const Bidi after = e < m ? dir(T(e)) : sos;
        const Bidi d = before == after ? before : sos;
        for (size_t q = k; q < e; q++) T(q) = d;
        k = e;
    }
    // I1/I2
    std::vector<uint8_t> lv(n, (uint8_t)para);
    for (size_t k = 0; k < m; k++) {
        const Bidi b = T(k);
        uint8_t l = (uint8_t)para;
        if (para & 1) {
            if (b == Bidi::L || b == Bidi::EN || b == Bidi::AN) l = (uint8_t)(para + 1);
        } else {
            if (b == Bidi::R) l = (uint8_t)(para + 1);
            else if (b == Bidi::AN || b == Bidi::EN) l = (uint8_t)(para + 2);
        }
        lv[idx[k]] = l;
    }
    // BN: the level of the previous character (or the paragraph level)
    for (size_t i = 0; i < n; i++)
        if (in[i] == Bidi::BN) lv[i] = i ? lv[i - 1] : (uint8_t)para;
    return lv;
}

std::vector<size_t> visual_order(std::vector<uint8_t> lv, const std::vector<bool>& ws, int para) {
    const size_t n = lv.size();
    // L1: trailing whitespace takes the paragraph level
    for (size_t i = n; i > 0 && i - 1 < ws.size() && ws[i - 1]; i--) lv[i - 1] = (uint8_t)para;
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    if (!n) return order;
    uint8_t hi = 0, lo_odd = 255;
    for (uint8_t l : lv) {
        hi = std::max(hi, l);
        if (l & 1) lo_odd = std::min(lo_odd, l);
    }
    // L2: from the highest level down to the lowest odd level, reverse every run at or above it
    for (int level = hi; level >= (int)lo_odd && level > 0; level--) {
        for (size_t i = 0; i < n;) {
            if (lv[order[i]] < level) {
                i++;
                continue;
            }
            size_t e = i;
            while (e < n && lv[order[e]] >= level) e++;
            std::reverse(order.begin() + (long)i, order.begin() + (long)e);
            i = e;
        }
    }
    return order;
}

// ---- a message ----

Plan plan(const uint16_t* s, size_t n, const HasGlyph& has) {
    Plan p;
    p.code.assign(s, s + n);
    p.skip.assign(n, 0);
    p.level.assign(n, 0);
    p.ws.assign(n, 0);
    // units: characters and control sequences
    std::vector<Bidi> cls(n, Bidi::BN);
    std::vector<uint8_t> is_char(n, 0);
    for (size_t i = 0; i < n;) {
        const uint16_t c = s[i];
        if (c == 0x0E && i + 3 < n) {  // tag: 0x0E group type size params
            const size_t len = 4 + s[i + 3] / 2;
            if (s[i + 1] == 3) cls[i] = Bidi::ON;  // a button icon: a neutral character
            i += len;
            continue;
        }
        if (c == 0x0F && i + 2 < n) {  // end tag: 0x0F group type
            i += 3;
            continue;
        }
        cls[i] = bidi_class(c);
        is_char[i] = 1;
        if (cls[i] == Bidi::R || cls[i] == Bidi::AL) p.rtl = true;
        i++;
    }
    if (!p.rtl) return p;
    // shaping, with tags and control codes breaking the joining (a tag's parameters are never letters)
    auto at = [&](size_t k) -> uint32_t { return is_char[k] ? s[k] : 0x0Bu; };
    for (size_t i = 0; i < n; i++) {
        if (!is_char[i]) continue;
        const Shaped r = shape_at(at, i, n, has);
        p.code[i] = r.code;
        if (r.skip) p.skip[i] = 1;
        p.ws[i] = cls[i] == Bidi::WS || cls[i] == Bidi::S;
    }
    // levels per paragraph (between line breaks)
    for (size_t a = 0; a < n;) {
        size_t b = a;
        while (b < n && !(is_char[b] && cls[b] == Bidi::B)) b++;
        std::vector<Bidi> pc(cls.begin() + (long)a, cls.begin() + (long)b);
        const std::vector<uint8_t> lv = resolve_levels(pc, 1);
        for (size_t k = a; k < b; k++) p.level[k] = lv[k - a];
        if (b < n) p.level[b] = 1;
        a = b + 1;
    }
    // L4: mirrored brackets at odd levels (the pairs are in the same fonts; `has` describes the
    // message font only, and the game's decorative brackets are in the menu font)
    for (size_t i = 0; i < n; i++)
        if (is_char[i] && (p.level[i] & 1) && mirror(s[i]) != s[i]) p.code[i] = mirror(s[i]);
    return p;
}

std::vector<float> reorder_line(const std::vector<Glyph>& g, int para) {
    const size_t n = g.size();
    std::vector<float> out(n);
    if (!n) return out;
    std::vector<uint8_t> lv(n);
    std::vector<bool> ws(n);
    for (size_t i = 0; i < n; i++) {
        lv[i] = g[i].level;
        ws[i] = g[i].ws;
    }
    const std::vector<size_t> order = visual_order(lv, ws, para);
    // the space after a glyph (letter spacing, kerning, a tag's room) up to its logical successor
    auto gap = [&](size_t i) { return i + 1 < n ? g[i + 1].pen - (g[i].pen + g[i].advance) : 0.0f; };
    float x = g[0].pen;
    for (size_t k = 0; k < n; k++) {
        const size_t i = order[k];
        out[i] = x;
        x += g[i].advance;
        if (k + 1 < n) {
            const size_t j = order[k + 1];
            // neighbours in both orders keep their gap; otherwise the gap that followed this glyph
            x += j == i + 1 ? gap(i) : j + 1 == i ? gap(j) : gap(i);
        }
    }
    return out;
}

}  // namespace rtl
