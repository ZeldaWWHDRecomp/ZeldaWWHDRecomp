/* SPDX-License-Identifier: MPL-2.0
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/gcmodel/, taken over
 * as it is; only the glue to this port changed. */
#include "gcm_io.h"

#include <algorithm>

namespace gcmodel {

Bytes yaz0_decode(const Bytes& d) {
    if (d.size() < 16 || memcmp(d.data(), "Yaz0", 4)) throw Error("not Yaz0");
    const uint32_t size = be32(&d[4]);
    if (size > kYaz0Max) throw Error("Yaz0 file too big");
    Bytes out;
    out.reserve(size);
    size_t p = 16;
    while (out.size() < size) {
        if (p >= d.size()) throw Error("Yaz0 data ends early");
        uint8_t code = d[p++];
        for (int b = 0; b < 8 && out.size() < size; b++, code <<= 1) {
            if (code & 0x80) {
                if (p >= d.size()) throw Error("Yaz0 data ends early");
                out.push_back(d[p++]);
                continue;
            }
            if (p + 1 >= d.size()) throw Error("Yaz0 data ends early");
            const uint8_t b1 = d[p], b2 = d[p + 1];
            p += 2;
            const size_t dist = ((b1 & 0xF) << 8 | b2) + 1;
            size_t n = b1 >> 4;
            if (n == 0) {
                if (p >= d.size()) throw Error("Yaz0 data ends early");
                n = d[p++] + 0x12;
            } else {
                n += 2;
            }
            if (dist > out.size()) throw Error("Yaz0 back reference out of range");
            const size_t st = out.size() - dist;
            for (size_t i = 0; i < n && out.size() < size; i++) out.push_back(out[st + i]);
        }
    }
    return out;
}

Bytes yaz0_encode(const Bytes& in, uint32_t align) {
    const size_t n = in.size();
    Bytes out = {'Y', 'a', 'z', '0'};
    out.resize(16, 0);
    put32(&out[4], (uint32_t)n);
    put32(&out[8], align);
    const int HASH = 1 << 16;
    std::vector<int32_t> head(HASH, -1), prev(n, -1);
    auto hash = [&](size_t p) { return ((in[p] << 8) ^ (in[p + 1] << 4) ^ in[p + 2]) & (HASH - 1); };
    auto insert = [&](size_t p) {
        if (p + 2 >= n) return;
        int h = hash(p);
        prev[p] = head[h];
        head[h] = (int32_t)p;
    };
    size_t p = 0;
    while (p < n) {
        const size_t codePos = out.size();
        out.push_back(0);
        uint8_t code = 0;
        for (int bit = 0; bit < 8 && p < n; bit++) {
            size_t bestLen = 0, bestDist = 0;
            if (p + 2 < n) {
                int chain = 0;
                for (int32_t q = head[hash(p)]; q >= 0 && p - q <= 0x1000 && chain < 256; q = prev[q], chain++) {
                    size_t l = 0;
                    const size_t maxl = std::min<size_t>(0x111, n - p);
                    while (l < maxl && in[q + l] == in[p + l]) l++;
                    if (l > bestLen) {
                        bestLen = l;
                        bestDist = p - q;
                        if (l == maxl) break;
                    }
                }
            }
            if (bestLen >= 3) {
                const size_t dd = bestDist - 1;
                if (bestLen >= 0x12) {
                    out.push_back((uint8_t)(dd >> 8));
                    out.push_back((uint8_t)dd);
                    out.push_back((uint8_t)(bestLen - 0x12));
                } else {
                    out.push_back((uint8_t)(((bestLen - 2) << 4) | (dd >> 8)));
                    out.push_back((uint8_t)dd);
                }
                for (size_t k = 0; k < bestLen; k++) insert(p + k);
                p += bestLen;
            } else {
                code |= 0x80 >> bit;
                out.push_back(in[p]);
                insert(p);
                p++;
            }
        }
        out[codePos] = code;
    }
    return out;
}

namespace {
struct SarcEntry {
    uint32_t attr, start, end;
    std::string name;
};

struct Sarc {
    uint16_t headerLen = 0;
    uint32_t dataOff = 0;
    std::vector<SarcEntry> ents;
};

Sarc sarc_parse(const Bytes& d) {
    if (d.size() < 0x14 || memcmp(d.data(), "SARC", 4)) throw Error("not a SARC");
    if (d[6] != 0xFE || d[7] != 0xFF) throw Error("little-endian SARC");
    Sarc s;
    s.headerLen = be16(&d[4]);
    s.dataOff = be32(&d[0xC]);
    const size_t p = s.headerLen;
    if (p + 12 > d.size() || memcmp(&d[p], "SFAT", 4)) throw Error("SARC without SFAT");
    const uint16_t n = be16(&d[p + 6]);
    if (p + 12 + (size_t)n * 16 > d.size()) throw Error("SARC table out of range");
    const size_t nt = p + 12 + (size_t)n * 16 + 8;
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t* e = &d[p + 12 + i * 16];
        SarcEntry en{be32(e + 4), be32(e + 8), be32(e + 12), ""};
        if (en.attr & 0x01000000) {
            size_t o = nt + (en.attr & 0xFFFF) * 4;
            while (o < d.size() && d[o]) en.name += (char)d[o++];
        }
        s.ents.push_back(en);
    }
    return s;
}
}  // namespace

std::map<std::string, Bytes> sarc_read(const Bytes& d) {
    Sarc s = sarc_parse(d);
    std::map<std::string, Bytes> out;
    for (auto& e : s.ents) {
        if (s.dataOff + e.end > d.size() || e.start > e.end) throw Error("SARC file out of range");
        out[e.name] = Bytes(d.begin() + s.dataOff + e.start, d.begin() + s.dataOff + e.end);
    }
    return out;
}

Bytes sarc_replace(const Bytes& d, const std::map<std::string, Bytes>& repl, uint32_t align) {
    Sarc s = sarc_parse(d);
    Bytes head(d.begin(), d.begin() + s.dataOff), body;
    for (size_t i = 0; i < s.ents.size(); i++) {
        auto& e = s.ents[i];
        auto it = repl.find(e.name);
        body.resize((body.size() + align - 1) / align * align, 0);
        const uint32_t start = (uint32_t)body.size();
        if (it != repl.end()) body.insert(body.end(), it->second.begin(), it->second.end());
        else body.insert(body.end(), d.begin() + s.dataOff + e.start, d.begin() + s.dataOff + e.end);
        put32(&head[s.headerLen + 12 + i * 16 + 8], start);
        put32(&head[s.headerLen + 12 + i * 16 + 12], (uint32_t)body.size());
    }
    Bytes out = head;
    out.insert(out.end(), body.begin(), body.end());
    put32(&out[8], (uint32_t)out.size());
    return out;
}

std::map<std::string, Bytes> rarc_read(const Bytes& in) {
    Bytes dec;
    const Bytes* dp = &in;
    if (in.size() >= 4 && !memcmp(in.data(), "Yaz0", 4)) {
        dec = yaz0_decode(in);
        dp = &dec;
    }
    const Bytes& d = *dp;
    if (d.size() < 0x40 || memcmp(d.data(), "RARC", 4)) throw Error("not a RARC archive (Link.arc)");
    const uint32_t dataOff = be32(&d[0xC]) + 0x20;
    const size_t info = 0x20;
    const uint32_t nfiles = be32(&d[info + 8]), fileOff = be32(&d[info + 0xC]) + 0x20, strOff = be32(&d[info + 0x14]) + 0x20;
    std::map<std::string, Bytes> out;
    for (uint32_t i = 0; i < nfiles; i++) {
        const size_t e = fileOff + (size_t)i * 20;
        if (e + 20 > d.size()) throw Error("RARC table out of range");
        const uint32_t tn = be32(&d[e + 4]), off = be32(&d[e + 8]), size = be32(&d[e + 12]);
        if ((tn >> 24) & 0x02) continue;  // a folder
        size_t o = strOff + (tn & 0xFFFFFF);
        std::string name;
        while (o < d.size() && d[o]) name += (char)d[o++];
        if ((size_t)dataOff + off + size > d.size()) throw Error("RARC file out of range: " + name);
        out[name] = Bytes(d.begin() + dataOff + off, d.begin() + dataOff + off + size);
    }
    return out;
}

}  // namespace gcmodel
