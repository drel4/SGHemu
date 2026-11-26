/* SHA-256 (FIPS 180-4), used to identify firmware and verify NVM snapshots. */

#include "port.h"
#include <string.h>

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotate_right(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void sha256_block(PlatSha256 *ctx, const uint8_t *block)
{
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned i;

    /* Read 16 big-endian words, then derive the rest of the message schedule. */
    for (i = 0; i < 16; ++i)
        words[i] = (uint32_t)block[i * 4] << 24 |
                   (uint32_t)block[i * 4 + 1] << 16 |
                   (uint32_t)block[i * 4 + 2] << 8 |
                   (uint32_t)block[i * 4 + 3];
    for (i = 16; i < 64; ++i) {
        uint32_t earlier = words[i - 15], later = words[i - 2];
        uint32_t s0 = rotate_right(earlier, 7) ^ rotate_right(earlier, 18) ^ (earlier >> 3);
        uint32_t s1 = rotate_right(later, 17) ^ rotate_right(later, 19) ^ (later >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    /* Working variables and round formulas follow FIPS 180-4, section 6.2.2. */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + sha256_k[i] + words[i];
        uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void plat_sha256_init(PlatSha256 *ctx)
{
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->bytes = 0;
    ctx->used = 0;
}

void plat_sha256_update(PlatSha256 *ctx, const void *data, size_t size)
{
    const uint8_t *p = data;

    ctx->bytes += size;
    while (size) {
        size_t room = sizeof(ctx->block) - ctx->used;
        size_t take = size < room ? size : room;

        memcpy(ctx->block + ctx->used, p, take);
        ctx->used += take;
        p += take;
        size -= take;
        if (ctx->used == sizeof(ctx->block)) {
            sha256_block(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

void plat_sha256_final(PlatSha256 *ctx, uint8_t digest[32])
{
    uint64_t bits = ctx->bytes * 8;
    uint8_t padding[sizeof(ctx->block)] = {0x80};
    uint8_t message_length[8];
    size_t length_offset = sizeof(ctx->block) - sizeof(message_length);
    unsigned i;

    /* Append one set bit, then zeros. Reserve the last eight bytes of the
       final block for the original length in bits, big-endian. */
    if (ctx->used >= length_offset)
        length_offset += sizeof(ctx->block);
    plat_sha256_update(ctx, padding, length_offset - ctx->used);
    for (i = 0; i < sizeof(message_length); ++i)
        message_length[i] = (uint8_t)(bits >> (56 - i * 8));
    plat_sha256_update(ctx, message_length, sizeof(message_length));

    for (i = 0; i < 8; ++i) {
        digest[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}
