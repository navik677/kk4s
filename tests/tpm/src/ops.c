/* Differential test functions for the x86 interpreter (compat/x86): built as a
   32-bit DLL with i686-w64-mingw32-gcc and natively into tests/tpm/x86_test.cpp;
   both must agree. Integer code only (the interpreter has no SSE / x87 maths). */
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
/* -nostdlib: the compiler may still emit calls to these */
void* memcpy(void* d, const void* s, size_t n) { uint8_t* a = d; const uint8_t* b = s; while (n--) *a++ = *b++; return d; }
void* memset(void* d, int c, size_t n) { uint8_t* a = d; while (n--) *a++ = (uint8_t)c; return d; }
void* memmove(void* d, const void* s, size_t n) {
    uint8_t* a = d; const uint8_t* b = s;
    if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
int __stdcall DllMain(void* h, uint32_t reason, void* p) { (void)h; (void)reason; (void)p; return 1; }
#else
#define EXPORT
#endif

EXPORT uint32_t op_arith(uint32_t a, uint32_t b) {
    uint32_t r = a * b + (a ^ (b >> 3)) - (b << 5);
    if (b) { r += a / b; r ^= a % b; }
    int32_t sa = (int32_t)a, sb = (int32_t)(b | 1);
    if (!(sa == INT32_MIN && sb == -1)) r += (uint32_t)(sa / sb) ^ (uint32_t)(sa % sb);
    r += (uint32_t)((int32_t)a >> (b & 31));
    uint32_t s = b & 31;
    r ^= (a << s) | (a >> ((32 - s) & 31));
    r += (uint16_t)a * (uint16_t)b;
    r ^= (uint32_t)((int16_t)a * (int16_t)(b >> 16));
    return r;
}

EXPORT uint32_t op_u64(uint32_t a, uint32_t b) {
    uint64_t x = (uint64_t)a * b + (((uint64_t)b << 32) | a);
    uint64_t y = x / ((uint64_t)b + 1);
    y ^= x % ((uint64_t)a | 1);
    int64_t sx = (int64_t)(x ^ ((uint64_t)a << 40));
    int64_t d = (int64_t)(int32_t)(b | 1);
    y += (uint64_t)(sx / d) ^ (uint64_t)(sx % d);
    y += x >> (a & 63);
    y ^= (uint64_t)((int64_t)x >> (b & 63));
    y += x << (b & 63);
    return (uint32_t)(y ^ (y >> 32));
}

EXPORT uint32_t op_bytes(uint32_t a, uint32_t b) {
    uint8_t c = (uint8_t)a, d = (uint8_t)b;
    uint32_t r = 0;
    for (int i = 0; i < 16; ++i) {
        c = (uint8_t)((c << 3) | (c >> 5));
        c ^= d;
        d = (uint8_t)(d + c * 7);
        if ((int8_t)c < (int8_t)d) r += c; else r ^= (uint32_t)d << (i & 7);
        r += (uint8_t)(c - d) > 100 ? 1u : 0u;
    }
    int8_t sc = (int8_t)a;
    r += (uint32_t)(int32_t)sc;
    r ^= (uint32_t)(uint16_t)(a >> 8) * 3u;
    return r;
}

EXPORT uint32_t op_bits(uint32_t a, uint32_t b) {
    uint32_t r = __builtin_bswap32(a) ^ b;
    if (a) r += (uint32_t)__builtin_ctz(a) + 33u * (uint32_t)__builtin_clz(a);
    r += (uint32_t)__builtin_popcount(a ^ b);
    uint32_t m = a;
    int n = 0;
    while (m) { m &= m - 1; ++n; }
    r ^= (uint32_t)n << 8;
    r += (a & (1u << (b & 31))) ? 7u : 13u;
    return r;
}

EXPORT uint32_t op_switch(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (uint32_t i = 0; i < 20; ++i) {
        switch ((a + i * b) % 13) {
        case 0: r += 3; break;
        case 1: r ^= a; break;
        case 2: r = r * 5 + 1; break;
        case 3: r -= b; break;
        case 4: r = (r << 1) | (r >> 31); break;
        case 5: r += i; break;
        case 6: r ^= 0x9E3779B9u; break;
        case 7: r = ~r; break;
        case 8: r >>= 1; break;
        case 9: r += a >> i; break;
        case 10: r ^= b << i; break;
        case 11: r = r * r; break;
        default: r += 12345; break;
        }
    }
    return r;
}

EXPORT uint32_t op_cmp(uint32_t a, uint32_t b) {
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    uint32_t r = 0;
    r |= (a < b) << 0;
    r |= (sa < sb) << 1;
    r |= (a == b) << 2;
    r |= (a >= b) << 3;
    r |= (sa >= sb) << 4;
    r |= (sa > 0) << 5;
    r |= ((a & b) != 0) << 6;
    uint32_t mn = a < b ? a : b, mx = sa > sb ? a : b;
    int32_t ab = sa < 0 ? -(sa + 1) : sa;
    return r + mn * 3 + mx * 5 + (uint32_t)ab;
}

/* a buffer filter shaped like an XP3 extraction callback body */
EXPORT void op_buf(uint8_t* buf, uint32_t n, uint32_t hash, uint32_t offset) {
    uint8_t tmp[64];
    for (uint32_t i = 0; i < 64; ++i) tmp[i] = (uint8_t)(hash >> (i & 24)) ^ (uint8_t)i;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t o = offset + i;
        uint8_t k = tmp[o & 63] + (uint8_t)(o >> 6);
        buf[i] = (uint8_t)((buf[i] ^ k) + (uint8_t)(hash >> 24));
    }
    if (n >= 8) {
        uint32_t w;
        memcpy(&w, buf, 4);
        w = (w >> 7) | (w << 25);
        memcpy(buf + 4, &w, 4);
    }
}

EXPORT uint32_t op_str(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}
