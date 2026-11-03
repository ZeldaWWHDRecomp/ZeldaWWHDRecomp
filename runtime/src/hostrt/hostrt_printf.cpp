// SPDX-License-Identifier: MPL-2.0
// printf family replacements (vsnprintf, snprintf, sprintf): a formatter written from the C
// standard's description of the conversions, reading the arguments through the guest's va_list like
// __va_arg (hostrt_libc.cpp). Results beyond the standard were measured on the recompiled originals
// (tools/host_runtime/difftest/calltest.cpp compares random formats and arguments).
//
// Floating-point conversions (%e %f %g %a and their capitals) are not replaced: a format containing one,
// or anything this formatter does not know, is handed back to the original before anything is written.
#include <cstring>
#include <string>

#include "hostrt_impl.h"

namespace hostrt {
namespace {

struct Args {  // the guest va_list {u8 gpr, u8 fpr, u16, u32 overflow, u32 save}
    uint32_t ap;
    uint32_t word() {
        uint32_t gpr = ld8(ap), addr;
        if (gpr < 8) {
            addr = ld32(ap + 8) + gpr * 4;
            st8(ap, (uint8_t)(gpr + 1));
        } else {
            addr = ld32(ap + 4);
            st32(ap + 4, addr + 4);
        }
        return ld32(addr);
    }
    uint64_t dword() {
        uint32_t gpr = (uint8_t)(ld8(ap) + (ld8(ap) & 1)), addr;
        if (gpr < 8) {
            addr = ld32(ap + 8) + gpr * 4;
            st8(ap, (uint8_t)(gpr + 2));
        } else {
            st8(ap, (uint8_t)gpr);
            addr = (ld32(ap + 4) + 7) & ~7u;
            st32(ap + 4, addr + 8);
        }
        return (uint64_t)ld32(addr) << 32 | ld32(addr + 4);
    }
};

struct Spec {
    bool minus = false, plus = false, space = false, hash = false, zero = false;
    int width = -1, prec = -1;
    int len = 0;  // 0 none, 1 h, 2 hh, 3 l, 4 ll
    char conv = 0;
};

// parses one conversion after '%'; returns false for anything not handled here
bool parse(uint32_t& f, Spec& s, bool& star_w, bool& star_p) {
    for (;; f++) {
        const char ch = (char)ld8(f);
        if (ch == '-') s.minus = true;
        else if (ch == '+') s.plus = true;
        else if (ch == ' ') s.space = true;
        else if (ch == '#') s.hash = true;
        else if (ch == '0') s.zero = true;
        else break;
    }
    star_w = star_p = false;
    if (ld8(f) == '*') star_w = true, f++;
    else
        while (ld8(f) >= '0' && ld8(f) <= '9') s.width = (s.width < 0 ? 0 : s.width) * 10 + (ld8(f++) - '0');
    if (ld8(f) == '.') {
        f++;
        s.prec = 0;
        if (ld8(f) == '*') star_p = true, f++;
        else
            while (ld8(f) >= '0' && ld8(f) <= '9') s.prec = s.prec * 10 + (ld8(f++) - '0');
    }
    if (ld8(f) == 'h') s.len = 1, f++;
    if (s.len == 1 && ld8(f) == 'h') s.len = 2, f++;
    if (!s.len && ld8(f) == 'l') s.len = 3, f++;
    if (s.len == 3 && ld8(f) == 'l') s.len = 4, f++;
    s.conv = (char)ld8(f);
    // wide characters/strings (%lc, %ls, ...) go through the multibyte conversion: handed back
    if ((s.conv == 'c' || s.conv == 's') && s.len) return false;
    return s.conv && strchr("diuoxXcs%", s.conv) && !(s.conv == '%' && (s.len || star_w || star_p));
}

}  // namespace

constexpr int kMaxField = 0x10000;  // larger widths/precisions are handed back (the original runs)
constexpr int kMaxDigits = 20;      // integer precision / zero-padded width handled here

// formats into a host string; false hands the call back (the va_list is then restored)
static bool format_once(uint32_t fmt, uint32_t ap, std::string& out);
static bool format(uint32_t fmt, uint32_t ap, std::string& out) {
    uint8_t saved[12];
    for (int i = 0; i < 12; i++) saved[i] = ld8(ap + i);
    if (format_once(fmt, ap, out)) return true;
    for (int i = 0; i < 12; i++) st8(ap + i, saved[i]);
    return false;
}

static bool format_once(uint32_t fmt, uint32_t ap, std::string& out) {
    // first pass: is every conversion handled?
    for (uint32_t f = fmt; ld8(f); f++) {
        if (ld8(f) != '%') continue;
        f++;
        Spec s;
        bool sw, sp;
        if (!parse(f, s, sw, sp)) return false;
    }
    Args a{ap};
    for (uint32_t f = fmt; ld8(f); f++) {
        const char ch = (char)ld8(f);
        if (ch != '%') {
            out += ch;
            continue;
        }
        f++;
        Spec s;
        bool sw, sp;
        parse(f, s, sw, sp);
        if (sw) {
            const int w = (int)a.word();
            if (w < -kMaxField || w > kMaxField) return false;
            if (w < 0) s.minus = true, s.width = -w;
            else s.width = w;
        }
        if (sp) {
            const int p = (int)a.word();
            if (p > kMaxField) return false;
            s.prec = p < 0 ? -1 : p;
        }
        if (s.width > kMaxField || s.prec > kMaxField) return false;
        std::string body, prefix;
        if (s.conv == '%') {
            body = "%";  // a field like %c (width and '-' apply, measured)
        } else if (s.conv == 'c') {
            body = std::string(1, (char)(uint8_t)a.word());
        } else if (s.conv == 's') {
            const uint32_t p = a.word();
            if (!p) {  // measured: a null string prints as "(null)"
                body = "(null)";
                if (s.prec >= 0 && s.prec < (int)body.size()) body.resize(s.prec);
            }
            for (uint32_t i = 0; p && (s.prec < 0 || (int)i < s.prec) && ld8(p + i); i++) body += (char)ld8(p + i);
        } else {
            // the original builds numbers in a small buffer; long precisions overrun it: handed back
            if (s.prec > kMaxDigits || (s.zero && s.width > kMaxDigits)) return false;
            const bool sgn = s.conv == 'd' || s.conv == 'i';
            uint64_t v = s.len == 4 ? a.dword() : a.word();
            if (s.len == 1) v = sgn ? (uint64_t)(int64_t)(int16_t)v : (uint16_t)v;
            else if (s.len == 2) v = sgn ? (uint64_t)(int64_t)(int8_t)v : (uint8_t)v;
            else if (s.len != 4) v = sgn ? (uint64_t)(int64_t)(int32_t)v : (uint32_t)v;
            bool neg = false;
            if (sgn && (int64_t)v < 0) neg = true, v = 0 - v;
            const unsigned base = s.conv == 'o' ? 8 : (s.conv == 'x' || s.conv == 'X') ? 16 : 10;
            const char* digits = s.conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            for (uint64_t x = v; x; x /= base) body.insert(body.begin(), digits[x % base]);
            if (s.prec >= 0) {
                while ((int)body.size() < s.prec) body.insert(body.begin(), '0');
            } else if (body.empty()) {
                body = "0";
            }
            if (s.hash && s.conv == 'o' && (body.empty() || body[0] != '0')) body.insert(body.begin(), '0');
            if (s.hash && v && (s.conv == 'x' || s.conv == 'X')) prefix = s.conv == 'x' ? "0x" : "0X";
            if (sgn) prefix = neg ? "-" : s.plus ? "+" : s.space ? " " : "";
            if (s.zero && !s.minus && s.prec < 0)
                while ((int)(prefix.size() + body.size()) < s.width) body.insert(body.begin(), '0');
        }
        // measured: '0' pads %c and %% with zeros too (%s with spaces)
        if (s.zero && !s.minus && (s.conv == 'c' || s.conv == '%'))
            while ((int)body.size() < s.width) body.insert(body.begin(), '0');
        std::string field = prefix + body;
        if ((int)field.size() < s.width) {
            if (s.minus) field.append(s.width - field.size(), ' ');
            else field.insert(0, s.width - field.size(), ' ');
        }
        out += field;
    }
    return true;
}

// vsnprintf(buf, n, fmt, ap): at most n-1 characters and a terminator (nothing for n == 0); r3 = the length
// of the whole result
bool vsnprintf_028F1748(Cpu* c) {
    std::string out;
    if (!format(c->r[5], c->r[6], out)) return false;
    const uint32_t buf = c->r[3], n = c->r[4];
    if (n) {
        const uint32_t k = out.size() < n - 1 ? (uint32_t)out.size() : n - 1;
        for (uint32_t i = 0; i < k; i++) st8(buf + i, (uint8_t)out[i]);
        st8(buf + k, 0);
    }
    st32(c->r[1] + 4, c->lr);
    c->r[3] = (uint32_t)out.size();
    c->r[4] = c->r[1] - 0x10;  // measured: the original leaves the address of its local output state here
    return true;
}

namespace {
// the varargs entry points build a va_list over their register arguments in their own frame (0x78
// bytes below SP): the GPRs r3..r10 at +0x18, the FPRs f1..f8 at +0x38 when the caller said there are
// float arguments (CR1.eq clear), {fixed GPR count, 0, overflow = SP+8, save area} at +8 (measured)
uint32_t build_va(Cpu* c, uint32_t fixed) {
    const uint32_t sp = c->r[1], frame = sp - 0x78;
    for (int i = 3; i <= 10; i++) st32(frame + 0x18 + 4 * (i - 3), c->r[i]);
    if (!c->cr[6])
        for (int i = 1; i <= 8; i++) st64(frame + 0x38 + 8 * (i - 1), f64_as_u64(c->f[i].ps0));
    st16(frame + 8, (uint16_t)(fixed << 8));
    st32(frame + 0xC, sp + 8);
    st32(frame + 0x10, frame + 0x18);
    st32(frame, sp);
    return frame + 8;
}
}  // namespace

// snprintf(buf, n, fmt, ...)
bool snprintf_028F17A8(Cpu* c) {
    const uint32_t sp = c->r[1];
    std::string out;
    // decide first (a hand-back must leave nothing behind): the va_list is built in the frame below SP
    const uint32_t va = build_va(c, 3);
    if (!format(c->r[5], va, out)) return false;
    st32(sp + 4, c->lr);
    const uint32_t buf = c->r[3], n = c->r[4];
    if (n) {
        const uint32_t k = out.size() < n - 1 ? (uint32_t)out.size() : n - 1;
        for (uint32_t i = 0; i < k; i++) st8(buf + i, (uint8_t)out[i]);
        st8(buf + k, 0);
    }
    c->r[3] = (uint32_t)out.size();
    c->r[4] = sp - 0x78 - 0x10;
    return true;
}

// sprintf(buf, fmt, ...)
bool sprintf_028F186C(Cpu* c) {
    const uint32_t sp = c->r[1];
    std::string out;
    const uint32_t va = build_va(c, 2);
    if (!format(c->r[4], va, out)) return false;
    st32(sp + 4, c->lr);
    const uint32_t buf = c->r[3];
    for (size_t i = 0; i < out.size(); i++) st8(buf + (uint32_t)i, (uint8_t)out[i]);
    st8(buf + (uint32_t)out.size(), 0);
    c->r[3] = (uint32_t)out.size();
    c->r[4] = sp - 0x78 - 0x8;  // measured (the inner vsprintf's local state)
    return true;
}

}  // namespace hostrt
