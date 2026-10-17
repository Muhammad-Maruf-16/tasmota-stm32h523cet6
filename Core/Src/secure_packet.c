#include "secure_packet.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SECRET_KEY "KUNCI_RAHASIA_STM32"

volatile int8_t g_crypto_status = 1;
static uint32_t s_last_seq = 0;

/* =========================================================================
 * Clean Pure-C SHA-256 (Standar FIPS PUB 180-4)
 * Bebas library eksternal, portable untuk Cortex-M33 / STM32H5
 * ========================================================================= */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
} SHA256_CTX;

#define DBL_INT_ADD(a,b,c) if (a > 0xffffffff - (c)) ++b; a += c;
#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))

#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[])
{
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) | ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    for ( ; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        if ((ctx->count % 64) == 0)
            sha256_transform(ctx, ctx->buffer);
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[])
{
    uint32_t i = ctx->count % 64;
    ctx->buffer[i++] = 0x80;
    if (i > 56) {
        while (i < 64) ctx->buffer[i++] = 0x00;
        sha256_transform(ctx, ctx->buffer);
        memset(ctx->buffer, 0, 56);
    } else {
        while (i < 56) ctx->buffer[i++] = 0x00;
    }
    uint64_t bits = ctx->count * 8;
    for (int b = 7; b >= 0; b--) {
        ctx->buffer[56 + (7 - b)] = (uint8_t)(bits >> (b * 8));
    }
    sha256_transform(ctx, ctx->buffer);

    for (i = 0; i < 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            hash[j * 4 + i] = (uint8_t)((ctx->state[j] >> (24 - i * 8)) & 0x000000ff);
        }
    }
}

static void ConvertBytesToHex(const uint8_t *bytes, size_t len, char *hex_out)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++)
    {
        hex_out[i * 2]     = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex_out[len * 2] = '\0';
}

void SecurePacket_Init(void)
{
    g_crypto_status = 1;
    s_last_seq = 0;
}

int Generate_Secure_Packet(const char *command, uint32_t raw_minute,
                           char *packet_buffer, size_t max_len)
{
    char string_to_hash[96];
    uint8_t digest[32];
    char hex_digest[65];
    int32_t modified_time = (int32_t)raw_minute - 100;
    int n = snprintf(string_to_hash, sizeof(string_to_hash), "%s:%s:%ld",
                     SECRET_KEY, command, (long)modified_time);
    if (n < 0 || (size_t)n >= sizeof(string_to_hash)) return -1;

    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)string_to_hash, (size_t)n);
    sha256_final(&ctx, digest);

    ConvertBytesToHex(digest, 32, hex_digest);
    n = snprintf(packet_buffer, max_len, "%s,%lu,%s\n", command, (unsigned long)raw_minute, hex_digest);
    return (n < 0 || (size_t)n >= max_len) ? -1 : 0;
}

int Verify_Secure_Command(const char *payload, char *cmd_out, size_t cmd_len)
{
    const char *c1 = strchr(payload, ',');
    if (!c1) return SECCMD_ERR_FORMAT;
    const char *c2 = strchr(c1 + 1, ',');
    if (!c2) return SECCMD_ERR_FORMAT;
    size_t clen = (size_t)(c1 - payload);
    size_t slen = (size_t)(c2 - c1 - 1);
    const char *hash_in = c2 + 1;
    if (clen == 0 || slen == 0 || strlen(hash_in) < 64) return SECCMD_ERR_FORMAT;

    char cmd[32];
    if (clen >= sizeof(cmd)) return SECCMD_ERR_FORMAT;
    memcpy(cmd, payload, clen);
    cmd[clen] = '\0';

    char seq_str[16];
    if (slen >= sizeof(seq_str)) return SECCMD_ERR_FORMAT;
    memcpy(seq_str, c1 + 1, slen);
    seq_str[slen] = '\0';
    uint32_t seq = (uint32_t)strtoul(seq_str, NULL, 10);

    char string_to_hash[96];
    int n = snprintf(string_to_hash, sizeof(string_to_hash), "%s:SECURE:%s:%lu",
                     SECRET_KEY, cmd, (unsigned long)seq);
    if (n < 0 || (size_t)n >= sizeof(string_to_hash)) return SECCMD_ERR_INTERNAL;

    uint8_t digest[32];
    char hex_digest[65];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)string_to_hash, (size_t)n);
    sha256_final(&ctx, digest);
    ConvertBytesToHex(digest, 32, hex_digest);

    if (strncmp(hex_digest, hash_in, 64) != 0) return SECCMD_ERR_AUTH;
    if (seq <= s_last_seq) return SECCMD_ERR_REPLAY;

    s_last_seq = seq;
    if (cmd_out && cmd_len > 0) {
        strncpy(cmd_out, cmd, cmd_len - 1);
        cmd_out[cmd_len - 1] = '\0';
    }
    return SECCMD_OK;
}
