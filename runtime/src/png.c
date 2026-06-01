/*
 * png.c -- a small, self-contained PNG decoder with a from-scratch DEFLATE
 * inflater (RFC 1950/1951). No zlib, no libpng -- in keeping with the project's
 * "the runtime is hand-written" rule. Outputs ARGB8888.
 *
 * Supports the color types/bit depths a 2005 J2ME game actually ships: palette
 * (1/2/4/8-bit, with tRNS transparency), truecolor RGB/RGBA (8-bit), and
 * grayscale +/- alpha (8-bit). 16-bit and interlaced PNGs are not expected.
 */
#include "j2me/runtime.h"
#include <stdlib.h>
#include <string.h>

/* ============================ DEFLATE inflate ============================== */
typedef struct {
    const uint8_t *src; size_t len, pos;
    uint32_t bitbuf; int bitcnt;
    uint8_t *out; size_t outlen, outcap;
} Inflate;

static int infl_grow(Inflate *z, size_t need) {
    if (z->outlen + need <= z->outcap) return 1;
    size_t cap = z->outcap ? z->outcap : 4096;
    while (cap < z->outlen + need) cap *= 2;
    uint8_t *n = (uint8_t *)realloc(z->out, cap);
    if (!n) return 0;
    z->out = n; z->outcap = cap; return 1;
}
static void infl_put(Inflate *z, uint8_t b) { if (infl_grow(z, 1)) z->out[z->outlen++] = b; }

static int getbit(Inflate *z) {
    if (z->bitcnt == 0) {
        if (z->pos >= z->len) return -1;
        z->bitbuf = z->src[z->pos++]; z->bitcnt = 8;
    }
    int b = z->bitbuf & 1; z->bitbuf >>= 1; z->bitcnt--; return b;
}
static int getbits(Inflate *z, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) { int b = getbit(z); if (b < 0) return -1; v |= b << i; }
    return v;
}

typedef struct { short counts[16]; short symbols[288]; } Huff;

static void huff_build(Huff *h, const uint8_t *lengths, int n) {
    memset(h->counts, 0, sizeof h->counts);
    for (int i = 0; i < n; i++) h->counts[lengths[i]]++;
    h->counts[0] = 0;
    short offs[16]; offs[0] = 0;
    for (int i = 1; i < 16; i++) offs[i] = offs[i - 1] + h->counts[i - 1];
    for (int i = 0; i < n; i++) if (lengths[i]) h->symbols[offs[lengths[i]]++] = (short)i;
}
static int huff_decode(Inflate *z, Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        int b = getbit(z); if (b < 0) return -1;
        code |= b;
        int count = h->counts[len];
        if (code - first < count) return h->symbols[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}

static const short LBASE[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short LEXT[]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short DBASE[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short DEXT[]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int inflate_block(Inflate *z, Huff *lit, Huff *dist) {
    for (;;) {
        int sym = huff_decode(z, lit);
        if (sym < 0) return -1;
        if (sym == 256) return 0;
        if (sym < 256) { infl_put(z, (uint8_t)sym); continue; }
        sym -= 257; if (sym >= 29) return -1;
        int length = LBASE[sym] + getbits(z, LEXT[sym]);
        int dsym = huff_decode(z, dist);
        if (dsym < 0 || dsym >= 30) return -1;
        int distance = DBASE[dsym] + getbits(z, DEXT[dsym]);
        if ((size_t)distance > z->outlen) return -1;
        if (!infl_grow(z, length)) return -1;
        size_t from = z->outlen - distance;
        for (int i = 0; i < length; i++) { z->out[z->outlen] = z->out[from + i]; z->outlen++; }
    }
}

static void fixed_tables(Huff *lit, Huff *dist) {
    uint8_t ll[288], dl[30];
    for (int i = 0; i < 144; i++) ll[i] = 8;
    for (int i = 144; i < 256; i++) ll[i] = 9;
    for (int i = 256; i < 280; i++) ll[i] = 7;
    for (int i = 280; i < 288; i++) ll[i] = 8;
    for (int i = 0; i < 30; i++) dl[i] = 5;
    huff_build(lit, ll, 288); huff_build(dist, dl, 30);
}

static int dynamic_tables(Inflate *z, Huff *lit, Huff *dist) {
    static const uint8_t ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int hlit = getbits(z, 5) + 257;
    int hdist = getbits(z, 5) + 1;
    int hclen = getbits(z, 4) + 4;
    uint8_t cl[19]; memset(cl, 0, sizeof cl);
    for (int i = 0; i < hclen; i++) cl[ORDER[i]] = (uint8_t)getbits(z, 3);
    Huff clh; huff_build(&clh, cl, 19);
    uint8_t lengths[288 + 30]; int n = 0, total = hlit + hdist;
    while (n < total) {
        int sym = huff_decode(z, &clh);
        if (sym < 0) return -1;
        if (sym < 16) lengths[n++] = (uint8_t)sym;
        else if (sym == 16) { int r = getbits(z, 2) + 3; uint8_t p = n ? lengths[n - 1] : 0; while (r-- && n < total) lengths[n++] = p; }
        else if (sym == 17) { int r = getbits(z, 3) + 3;  while (r-- && n < total) lengths[n++] = 0; }
        else { int r = getbits(z, 7) + 11; while (r-- && n < total) lengths[n++] = 0; }
    }
    huff_build(lit, lengths, hlit);
    huff_build(dist, lengths + hlit, hdist);
    return 0;
}

/* inflate a raw DEFLATE stream; returns malloc'd bytes + length, or NULL */
static uint8_t *inflate_raw(const uint8_t *src, size_t len, size_t *out_len) {
    Inflate z; memset(&z, 0, sizeof z);
    z.src = src; z.len = len;
    for (;;) {
        int bfinal = getbit(&z); if (bfinal < 0) break;
        int btype = getbits(&z, 2);
        if (btype == 0) {
            z.bitcnt = 0;                 /* align to byte boundary */
            if (z.pos + 4 > z.len) break;
            int blen = z.src[z.pos] | (z.src[z.pos + 1] << 8); z.pos += 4;  /* skip NLEN */
            if (!infl_grow(&z, blen)) break;
            for (int i = 0; i < blen && z.pos < z.len; i++) z.out[z.outlen++] = z.src[z.pos++];
        } else if (btype == 1 || btype == 2) {
            Huff lit, dist;
            if (btype == 1) fixed_tables(&lit, &dist);
            else if (dynamic_tables(&z, &lit, &dist) != 0) break;
            if (inflate_block(&z, &lit, &dist) != 0) break;
        } else break;     /* reserved */
        if (bfinal) break;
    }
    *out_len = z.outlen;
    return z.out;
}

/* =============================== PNG ======================================= */
static uint32_t rd32(const uint8_t *p) { return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

uint32_t *png_decode(const uint8_t *data, int len, int *ow, int *oh) {
    if (len < 8 || data[0] != 0x89 || data[1] != 'P') return NULL;
    int W = 0, H = 0, bitdepth = 8, color = 6;
    uint8_t palette[256][3]; int paln = 0;
    uint8_t trns[256]; int trnsn = 0; memset(trns, 255, sizeof trns);

    /* concatenate IDAT */
    uint8_t *idat = NULL; size_t idat_len = 0;
    size_t p = 8;
    while (p + 8 <= (size_t)len) {
        uint32_t clen = rd32(data + p);
        const uint8_t *type = data + p + 4;
        const uint8_t *body = data + p + 8;
        if (p + 12 + clen > (size_t)len) break;
        if (!memcmp(type, "IHDR", 4)) {
            W = (int)rd32(body); H = (int)rd32(body + 4);
            bitdepth = body[8]; color = body[9];
            if (body[12]) { /* interlaced: unsupported */ free(idat); return NULL; }
        } else if (!memcmp(type, "PLTE", 4)) {
            paln = (int)clen / 3; if (paln > 256) paln = 256;
            for (int i = 0; i < paln; i++) { palette[i][0]=body[i*3]; palette[i][1]=body[i*3+1]; palette[i][2]=body[i*3+2]; }
        } else if (!memcmp(type, "tRNS", 4)) {
            trnsn = (int)clen; if (trnsn > 256) trnsn = 256;
            for (int i = 0; i < trnsn; i++) trns[i] = body[i];
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *n = (uint8_t *)realloc(idat, idat_len + clen);
            if (!n) { free(idat); return NULL; }
            idat = n; memcpy(idat + idat_len, body, clen); idat_len += clen;
        } else if (!memcmp(type, "IEND", 4)) break;
        p += 12 + clen;   /* 4 len + 4 type + body + 4 CRC */
    }
    if (!idat || W <= 0 || H <= 0 || W > 4096 || H > 4096) { free(idat); return NULL; }

    /* zlib wrapper: 2-byte header, optional preset dict, trailing adler32 */
    size_t raw_len = 0;
    size_t skip = 2 + ((idat_len >= 2 && (idat[1] & 0x20)) ? 4 : 0);
    uint8_t *raw = inflate_raw(idat + skip, idat_len - skip, &raw_len);
    free(idat);
    if (!raw) return NULL;

    int channels = (color == 2) ? 3 : (color == 6) ? 4 : (color == 4) ? 2 : 1;
    int bpp = (channels * bitdepth + 7) / 8;             /* bytes per pixel for filtering */
    size_t rowbytes = ((size_t)W * channels * bitdepth + 7) / 8;
    if (raw_len < (rowbytes + 1) * (size_t)H) { free(raw); return NULL; }

    uint32_t *argb = (uint32_t *)malloc((size_t)W * H * sizeof(uint32_t));
    if (!argb) { free(raw); return NULL; }

    /* unfilter in place, row by row */
    uint8_t *prev = (uint8_t *)calloc(1, rowbytes);
    uint8_t *cur = (uint8_t *)malloc(rowbytes ? rowbytes : 1);
    uint8_t *sp = raw;
    for (int y = 0; y < H; y++) {
        int filter = *sp++;
        memcpy(cur, sp, rowbytes); sp += rowbytes;
        for (size_t i = 0; i < rowbytes; i++) {
            int a = (i >= (size_t)bpp) ? cur[i - bpp] : 0;
            int b = prev[i];
            int c = (i >= (size_t)bpp) ? prev[i - bpp] : 0;
            switch (filter) {
                case 1: cur[i] = (uint8_t)(cur[i] + a); break;
                case 2: cur[i] = (uint8_t)(cur[i] + b); break;
                case 3: cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1)); break;
                case 4: cur[i] = (uint8_t)(cur[i] + paeth(a, b, c)); break;
                default: break;
            }
        }
        /* emit pixels */
        for (int x = 0; x < W; x++) {
            uint32_t pix;
            if (color == 3) {                 /* palette */
                int idx;
                if (bitdepth == 8) idx = cur[x];
                else { int per = 8 / bitdepth, shift = (per - 1 - (x % per)) * bitdepth;
                       idx = (cur[x / per] >> shift) & ((1 << bitdepth) - 1); }
                uint8_t r = palette[idx][0], g = palette[idx][1], bl = palette[idx][2];
                uint8_t al = (idx < trnsn) ? trns[idx] : 255;
                pix = ((uint32_t)al << 24) | (r << 16) | (g << 8) | bl;
            } else if (color == 2) {          /* RGB */
                uint8_t *q = cur + x * 3; pix = 0xFF000000u | (q[0] << 16) | (q[1] << 8) | q[2];
            } else if (color == 6) {          /* RGBA */
                uint8_t *q = cur + x * 4; pix = ((uint32_t)q[3] << 24) | (q[0] << 16) | (q[1] << 8) | q[2];
            } else if (color == 4) {          /* gray + alpha */
                uint8_t *q = cur + x * 2; pix = ((uint32_t)q[1] << 24) | (q[0] << 16) | (q[0] << 8) | q[0];
            } else {                          /* grayscale */
                uint8_t g8;
                if (bitdepth == 8) g8 = cur[x];
                else { int per = 8 / bitdepth, shift = (per - 1 - (x % per)) * bitdepth;
                       int v = (cur[x / per] >> shift) & ((1 << bitdepth) - 1);
                       g8 = (uint8_t)(v * 255 / ((1 << bitdepth) - 1)); }
                pix = 0xFF000000u | (g8 << 16) | (g8 << 8) | g8;
            }
            argb[y * W + x] = pix;
        }
        uint8_t *t = prev; prev = cur; cur = t;
    }
    free(prev); free(cur); free(raw);
    *ow = W; *oh = H;
    return argb;
}
