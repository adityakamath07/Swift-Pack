#include "swiftpack.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

#define HEADER_SIZE 12u
#define HASH_SIZE 65536u
#define MIN_MATCH 4u
#define MAX_MATCH 131u
#define MAX_OFFSET 65535u

typedef struct { unsigned char *data; size_t size, capacity; } Buffer;

static int reserve(Buffer *b, size_t extra) {
    size_t needed, cap;
    unsigned char *p;
    if (extra > SIZE_MAX - b->size) return 0;
    needed = b->size + extra;
    if (needed <= b->capacity) return 1;
    cap = b->capacity ? b->capacity : 64;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) { cap = needed; break; }
        cap *= 2;
    }
    p = (unsigned char *)realloc(b->data, cap);
    if (!p) return 0;
    b->data = p; b->capacity = cap;
    return 1;
}

static int byte(Buffer *b, unsigned char v) {
    if (!reserve(b, 1)) return 0;
    b->data[b->size++] = v; return 1;
}

static unsigned hash3(const unsigned char *p) {
    uint32_t v = (uint32_t)p[0] * 251u + (uint32_t)p[1] * 31u + p[2];
    return (unsigned)((v * 2654435761u) >> 16);
}

static uint32_t crc32(const unsigned char *data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

/* Compare match candidates 16 bytes at a time when the target supports SSE2. */
static size_t match_length(const unsigned char *a, const unsigned char *b,
                           size_t limit) {
    size_t n = 0;
#if defined(__SSE2__)
    while (limit - n >= 16) {
        __m128i va = _mm_loadu_si128((const __m128i *)(const void *)(a + n));
        __m128i vb = _mm_loadu_si128((const __m128i *)(const void *)(b + n));
        unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb));
        if (mask != 0xffffu) {
            for (size_t k = 0; k < 16; ++k)
                if (!(mask & (1u << k))) return n + k;
        }
        n += 16;
    }
#endif
    while (n < limit && a[n] == b[n]) ++n;
    return n;
}

static int emit_literals(Buffer *b, const unsigned char *p, size_t n) {
    while (n) {
        size_t part = n > 128 ? 128 : n;
        if (!byte(b, (unsigned char)(part - 1)) || !reserve(b, part)) return 0;
        memcpy(b->data + b->size, p, part); b->size += part;
        p += part; n -= part;
    }
    return 1;
}

int swiftpack_compress(const unsigned char *in, size_t n,
                       unsigned char **output, size_t *output_size) {
    Buffer b = {0};
    int32_t *table;
    size_t i = 0, literal = 0;
    static const unsigned char empty = 0;
    if (!output || !output_size || (!in && n)) return 0;
    if (!in) in = &empty;
    *output = NULL; *output_size = 0;
    if (n > INT32_MAX || n > 0x00ffffffffffffffULL || !reserve(&b, HEADER_SIZE)) return 0;
    memcpy(b.data, "SWPK", 4); b.data[4] = 2;
    for (unsigned k = 0; k < 7; ++k) b.data[5+k] = (unsigned char)(((uint64_t)n >> (8*k)) & 255u);
    b.size = HEADER_SIZE;
    table = (int32_t *)malloc(HASH_SIZE * sizeof(*table));
    if (!table) { free(b.data); return 0; }
    for (size_t k = 0; k < HASH_SIZE; ++k) table[k] = -1;

    while (i + MIN_MATCH <= n) {
        unsigned h = hash3(in + i);
        int32_t prior = table[h];
        size_t match = 0, offset = 0;
        table[h] = (int32_t)i;
        if (prior >= 0 && i - (size_t)prior <= MAX_OFFSET) {
            offset = i - (size_t)prior;
            size_t limit = n - i;
            if (limit > MAX_MATCH) limit = MAX_MATCH;
            match = match_length(in + (size_t)prior, in + i, limit);
        }
        if (match >= MIN_MATCH) {
            if (!emit_literals(&b, in + literal, i - literal) ||
                !byte(&b, (unsigned char)(0x80u | (match - MIN_MATCH))) ||
                !byte(&b, (unsigned char)(offset & 255u)) ||
                !byte(&b, (unsigned char)(offset >> 8))) goto fail;
            for (size_t k = 1; k < match && i + k + MIN_MATCH <= n; ++k)
                table[hash3(in + i + k)] = (int32_t)(i + k);
            i += match; literal = i;
        } else ++i;
    }
    if (!emit_literals(&b, in + literal, n - literal)) goto fail;
    {
        uint32_t checksum = crc32(in, n);
        for (unsigned k = 0; k < 4; ++k)
            if (!byte(&b, (unsigned char)(checksum >> (8*k)))) goto fail;
    }
    free(table); *output = b.data; *output_size = b.size; return 1;
fail:
    free(table); free(b.data); return 0;
}

int swiftpack_decompress(const unsigned char *in, size_t n,
                         unsigned char **output, size_t *output_size) {
    uint64_t declared = 0;
    size_t pos = HEADER_SIZE, outpos = 0, payload_end;
    unsigned version;
    unsigned char *out;
    if (!output || !output_size || !in || n < HEADER_SIZE) return 0;
    *output = NULL; *output_size = 0;
    if (memcmp(in, "SWPK", 4) || (in[4] != 1 && in[4] != 2)) return 0;
    version = in[4];
    if (version == 2 && n < HEADER_SIZE + 4) return 0;
    payload_end = n - (version == 2 ? 4u : 0u);
    for (unsigned k = 0; k < 7; ++k) declared |= (uint64_t)in[5+k] << (8*k);
    if (declared > SIZE_MAX) return 0;
    out = (unsigned char *)malloc(declared ? (size_t)declared : 1);
    if (!out) return 0;
    while (pos < payload_end) {
        unsigned token = in[pos++];
        if (!(token & 0x80u)) {
            size_t len = (token & 0x7fu) + 1u;
            if (len > payload_end-pos || len > (size_t)declared-outpos) goto bad;
            memcpy(out+outpos, in+pos, len); pos += len; outpos += len;
        } else {
            size_t len = (token & 0x7fu) + MIN_MATCH, off;
            if (payload_end-pos < 2) goto bad;
            off = (size_t)in[pos] | ((size_t)in[pos+1] << 8); pos += 2;
            if (!off || off > outpos || len > (size_t)declared-outpos) goto bad;
            for (size_t k = 0; k < len; ++k) out[outpos+k] = out[outpos+k-off];
            outpos += len;
        }
    }
    if (outpos != (size_t)declared) goto bad;
    if (version == 2) {
        uint32_t expected = 0;
        for (unsigned k = 0; k < 4; ++k) expected |= (uint32_t)in[payload_end+k] << (8*k);
        if (crc32(out, outpos) != expected) goto bad;
    }
    *output = out; *output_size = outpos; return 1;
bad:
    free(out); return 0;
}
