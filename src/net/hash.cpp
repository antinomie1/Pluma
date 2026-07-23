#include "net/hash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace net {
namespace {

// Minimal self-contained SHA-1 (FIPS 180-1), hand-rolled so the download
// subsystem links no external crypto library: TLS is handled by libcurl's
// OS-native backend (Schannel on Windows, OpenSSL on Linux), and file hashing
// -- needed only to verify/skip already-downloaded artifacts against the sha1
// fields in Mojang/BMCLAPI manifests -- is the one other thing that needed a
// digest. SHA-1 is cryptographically retired, but that's irrelevant here: it's
// used purely to match the hashes the manifests themselves publish.
struct Sha1Ctx {
    std::uint32_t state[5];
    std::uint64_t count; // total bytes fed so far
    unsigned char buffer[64];
};

inline std::uint32_t Rol(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

void Sha1Transform(std::uint32_t state[5], const unsigned char block[64]) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        const std::uint32_t tmp = Rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rol(b, 30);
        b = a;
        a = tmp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void Sha1Init(Sha1Ctx& ctx) {
    ctx.state[0] = 0x67452301;
    ctx.state[1] = 0xEFCDAB89;
    ctx.state[2] = 0x98BADCFE;
    ctx.state[3] = 0x10325476;
    ctx.state[4] = 0xC3D2E1F0;
    ctx.count = 0;
}

void Sha1Update(Sha1Ctx& ctx, const unsigned char* data, std::size_t len) {
    std::size_t buffered = static_cast<std::size_t>(ctx.count % 64);
    ctx.count += len;
    if (buffered > 0) {
        const std::size_t need = 64 - buffered;
        if (len < need) {
            std::memcpy(ctx.buffer + buffered, data, len);
            return;
        }
        std::memcpy(ctx.buffer + buffered, data, need);
        Sha1Transform(ctx.state, ctx.buffer);
        data += need;
        len -= need;
    }
    while (len >= 64) {
        Sha1Transform(ctx.state, data);
        data += 64;
        len -= 64;
    }
    if (len > 0) std::memcpy(ctx.buffer, data, len);
}

void Sha1Final(Sha1Ctx& ctx, unsigned char digest[20]) {
    const std::uint64_t bit_len = ctx.count * 8;
    // Append 0x80 then zero-pad (one byte at a time -- at most ~64 iterations)
    // until the length field lands at the 56-byte mark of the final block.
    unsigned char byte = 0x80;
    Sha1Update(ctx, &byte, 1);
    byte = 0x00;
    while (ctx.count % 64 != 56) {
        Sha1Update(ctx, &byte, 1);
    }
    unsigned char len_bytes[8];
    for (int i = 0; i < 8; ++i) {
        len_bytes[i] = static_cast<unsigned char>((bit_len >> (56 - i * 8)) & 0xFF);
    }
    Sha1Update(ctx, len_bytes, 8);

    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = static_cast<unsigned char>((ctx.state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<unsigned char>((ctx.state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<unsigned char>((ctx.state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<unsigned char>((ctx.state[i]) & 0xFF);
    }
}

// Hex-encodes a 20-byte SHA1 digest, lowercase (matching how Mojang/BMCLAPI
// report sha1 fields in their manifests -- comparisons are plain string
// equality against those).
std::string ToHex(const unsigned char digest[20]) {
    static const char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.resize(40);
    for (int i = 0; i < 20; ++i) {
        out[static_cast<std::size_t>(i) * 2] = kHexDigits[digest[i] >> 4];
        out[static_cast<std::size_t>(i) * 2 + 1] = kHexDigits[digest[i] & 0x0F];
    }
    return out;
}

} // namespace

std::string Sha1File(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return std::string();

    Sha1Ctx ctx;
    Sha1Init(ctx);

    std::array<unsigned char, 64 * 1024> buffer{};
    std::size_t read = 0;
    while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
        Sha1Update(ctx, buffer.data(), read);
    }
    const bool ok = std::feof(file) != 0;
    std::fclose(file);
    if (!ok) return std::string();

    unsigned char digest[20];
    Sha1Final(ctx, digest);
    return ToHex(digest);
}

bool VerifyFile(const std::string& path, std::uint64_t expected_size,
                const std::string& expected_sha1) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return false;

    if (expected_size > 0) {
        const std::uintmax_t actual_size = fs::file_size(path, ec);
        if (ec || actual_size != expected_size) return false;
    }

    if (expected_sha1.empty()) return true; // nothing further to check against

    const std::string actual_sha1 = Sha1File(path);
    return !actual_sha1.empty() && actual_sha1 == expected_sha1;
}

namespace {

// Minimal self-contained MD5 (RFC 1321). Not a security context -- see
// OfflineUuid's header doc; it's only the deterministic mapping the Minecraft
// ecosystem uses for offline player UUIDs. Operates on the whole (short)
// string at once rather than streaming, since the only input is
// "OfflinePlayer:<name>".
inline std::uint32_t Md5Rol(std::uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

std::array<unsigned char, 16> Md5(const std::string& msg) {
    static const std::uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
        0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
        0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
        0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
        0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
        0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
        0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

    std::vector<unsigned char> data(msg.begin(), msg.end());
    const std::uint64_t bit_len = static_cast<std::uint64_t>(msg.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0x00);
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<unsigned char>((bit_len >> (8 * i)) & 0xFF)); // little-endian
    }

    for (std::size_t off = 0; off < data.size(); off += 64) {
        std::uint32_t M[16];
        for (int i = 0; i < 16; ++i) {
            M[i] = static_cast<std::uint32_t>(data[off + i * 4]) |
                   (static_cast<std::uint32_t>(data[off + i * 4 + 1]) << 8) |
                   (static_cast<std::uint32_t>(data[off + i * 4 + 2]) << 16) |
                   (static_cast<std::uint32_t>(data[off + i * 4 + 3]) << 24);
        }
        std::uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; ++i) {
            std::uint32_t F;
            int g;
            if (i < 16) {
                F = (B & C) | (~B & D);
                g = i;
            } else if (i < 32) {
                F = (D & B) | (~D & C);
                g = (5 * i + 1) & 15;
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (3 * i + 5) & 15;
            } else {
                F = C ^ (B | ~D);
                g = (7 * i) & 15;
            }
            const std::uint32_t tmp = D;
            D = C;
            C = B;
            B = B + Md5Rol(A + F + K[i] + M[g], S[i]);
            A = tmp;
        }
        a0 += A;
        b0 += B;
        c0 += C;
        d0 += D;
    }

    std::array<unsigned char, 16> out{};
    const std::uint32_t vals[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[static_cast<std::size_t>(i) * 4 + j] =
                static_cast<unsigned char>((vals[i] >> (8 * j)) & 0xFF);
        }
    }
    return out;
}

} // namespace

std::string OfflineUuid(const std::string& name) {
    std::array<unsigned char, 16> h = Md5("OfflinePlayer:" + name);
    h[6] = static_cast<unsigned char>((h[6] & 0x0F) | 0x30); // version 3
    h[8] = static_cast<unsigned char>((h[8] & 0x3F) | 0x80); // RFC 4122 variant

    static const char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {
        out.push_back(kHexDigits[h[i] >> 4]);
        out.push_back(kHexDigits[h[i] & 0x0F]);
        if (i == 3 || i == 5 || i == 7 || i == 9) out.push_back('-');
    }
    return out;
}

} // namespace net
