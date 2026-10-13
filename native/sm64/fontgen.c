// TES64: Skyrim's text in Super Mario 64's dialogue font, made on the player's PC from the player's own ROM. Nothing of
// the game's is shipped: this reads the glyph bitmaps out of the ROM's segment-2 bank (US version) and writes a
// Scaleform font library (a small SWF holding one DefineFont3) that Skyrim's fontconfig.txt can point its text at.
//
// The US dialogue font is 92 glyphs of 16x8 4-bit IA pixels, 64 bytes each, from 0x5900 of the MIO0 bank at ROM 0x108A40,
// stored on their sides: turned a quarter, each is an 8x16 letter. A glyph pixel is drawn when its alpha bit is set; runs
// of them become rectangles, so the font scales up crisp.
//
// Used by sm64.dll (tes64_font_generate, for the plugin) and by tes64_fontgen.exe (for the installer).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BANK_ROM 0x108A40u
#define FONT_OFFSET 0x5900u
#define FONT_GLYPHS 92
#define GW 8      // an upright glyph: 8 wide, 16 tall
#define GH 16
#define BASELINE 13   // the row under the letters' feet (capitals fill rows 3-12)
#define UNIT 1280     // twips per glyph pixel: DefineFont3's em is 20480 twips, 16 pixels tall

static char s_error[160];
const char *fontgen_error(void) { return s_error; }
static int fail(const char *msg) { snprintf(s_error, sizeof(s_error), "%s", msg); return 0; }

// ---- MIO0 -------------------------------------------------------------------------------------------------------
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint8_t *mio0_decode(const uint8_t *rom, size_t len, uint32_t off, uint32_t *outSize)
{
    if (off + 16 > len || memcmp(rom + off, "MIO0", 4)) { fail("the ROM has no compressed bank where a US Super Mario 64 ROM has it"); return NULL; }
    uint32_t size = be32(rom + off + 4), co = be32(rom + off + 8), uo = be32(rom + off + 12);
    if (size < 0x7200 || size > 0x400000 || off + co > len || off + uo > len) { fail("the ROM's graphics bank looks wrong"); return NULL; }
    uint8_t *out = malloc(size);
    if (!out) { fail("out of memory"); return NULL; }
    size_t o = 0, li = off + 16, ci = off + co, ui = off + uo;
    uint32_t flags = 0, mask = 0;
    while (o < size) {
        if (!mask) {
            if (li + 4 > len) { free(out); fail("truncated bank"); return NULL; }
            flags = be32(rom + li); li += 4; mask = 0x80000000u;
        }
        if (flags & mask) {
            if (ui >= len) { free(out); fail("truncated bank"); return NULL; }
            out[o++] = rom[ui++];
        } else {
            if (ci + 2 > len) { free(out); fail("truncated bank"); return NULL; }
            unsigned v = rom[ci] << 8 | rom[ci + 1]; ci += 2;
            unsigned n = (v >> 12) + 3, dist = (v & 0xFFF) + 1;
            if (dist > o) { free(out); fail("corrupt bank"); return NULL; }
            for (unsigned k = 0; k < n && o < size; k++, o++) out[o] = out[o - dist];
        }
        mask >>= 1;
    }
    *outSize = size;
    return out;
}

// ---- glyphs -----------------------------------------------------------------------------------------------------
typedef struct { int cp; uint8_t px[GH][GW]; int adv; } Glyph;   // px[y][x], y down

static Glyph s_glyphs[256];
static int s_count;

// the ROM's glyph, turned upright: the stored 16x8 picture is on its side, a quarter turn and a flip from upright
static void rom_glyph(const uint8_t *bank, int index, uint8_t px[GH][GW])
{
    const uint8_t *g = bank + FONT_OFFSET + index * 64;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            int row = GW - 1 - x, col = GH - 1 - y;
            uint8_t v = g[(row * 16 + col) / 2];
            uint8_t nib = (col & 1) ? (v & 15) : (v >> 4);
            px[y][x] = nib & 1;   // IA4: the low bit is alpha
        }
}

static int bounds_x(const uint8_t px[GH][GW], int *minx, int *maxx)
{
    int lo = GW, hi = -1;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            if (px[y][x]) { if (x < lo) lo = x; if (x > hi) hi = x; }
    *minx = lo; *maxx = hi;
    return hi >= 0;
}

static void add_glyph(int cp, const uint8_t px[GH][GW])
{
    if (s_count >= 256) return;
    Glyph *g = &s_glyphs[s_count++];
    g->cp = cp;
    memcpy(g->px, px, sizeof(g->px));
    int lo, hi;
    g->adv = bounds_x(px, &lo, &hi) ? hi + 2 : 5;   // a pixel of space after the letter
    if (g->adv < 4) g->adv = 4;
}

// the few marks the ROM's font lacks (SM64's dialogue has no hyphen or colon of its own here), drawn plainly
static void hline(uint8_t px[GH][GW], int y, int x0, int x1) { for (int x = x0; x <= x1; x++) px[y][x] = 1; }
static void vline(uint8_t px[GH][GW], int x, int y0, int y1) { for (int y = y0; y <= y1; y++) px[y][x] = 1; }
static void made(int cp)
{
    uint8_t p[GH][GW];
    memset(p, 0, sizeof(p));
    switch (cp) {
    case ' ': break;
    case '-': hline(p, 8, 1, 5); break;
    case ':': p[7][2] = p[8][2] = 1; p[11][2] = p[12][2] = 1; break;
    case ';': p[7][2] = p[8][2] = 1; p[11][2] = p[12][2] = p[13][1] = 1; break;
    case '+': hline(p, 8, 1, 5); vline(p, 3, 6, 10); break;
    case '=': hline(p, 7, 1, 5); hline(p, 10, 1, 5); break;
    case '_': hline(p, 14, 0, 6); break;
    case '|': vline(p, 3, 3, 15); break;
    case '[': vline(p, 2, 3, 14); hline(p, 3, 3, 4); hline(p, 14, 3, 4); break;
    case ']': vline(p, 4, 3, 14); hline(p, 3, 2, 3); hline(p, 14, 2, 3); break;
    case '{': vline(p, 3, 3, 14); hline(p, 3, 4, 5); hline(p, 14, 4, 5); p[8][2] = 1; break;
    case '}': vline(p, 3, 3, 14); hline(p, 3, 1, 2); hline(p, 14, 1, 2); p[8][4] = 1; break;
    case '<': for (int i = 0; i < 4; i++) { p[6 + i][4 - i] = 1; p[11 - i][4 - i] = 1; } break;
    case '>': for (int i = 0; i < 4; i++) { p[6 + i][1 + i] = 1; p[11 - i][1 + i] = 1; } break;
    case '#': vline(p, 2, 4, 13); vline(p, 5, 4, 13); hline(p, 6, 1, 6); hline(p, 10, 1, 6); break;
    case '\\': for (int i = 0; i < 10; i++) p[3 + i][1 + i / 2] = 1; break;
    case '^': for (int i = 0; i < 3; i++) { p[3 + i][3 - i] = 1; p[3 + i][3 + i] = 1; } break;
    case '`': p[3][2] = p[4][3] = 1; break;
    case '@': hline(p, 4, 2, 5); hline(p, 12, 2, 5); vline(p, 1, 5, 11); vline(p, 6, 5, 11); hline(p, 8, 3, 5); vline(p, 3, 8, 10); break;
    default: return;
    }
    add_glyph(cp, p);
}

static int cmp_glyph(const void *a, const void *b) { return ((const Glyph *)a)->cp - ((const Glyph *)b)->cp; }

static int build_glyphs(const uint8_t *bank)
{
    static const struct { int index, cp, cp2, cp3; } map[] = {
        { 62, 0x2194, 0, 0 }, { 63, '!', 0, 0 }, { 64, '$', 0, 0 }, { 65, 0xD7, 0, 0 }, { 66, '(', 0, 0 }, { 68, ')', 0, 0 },
        { 69, '~', 0, 0 }, { 70, '.', 0, 0 }, { 71, '%', 0, 0 }, { 72, 0xB7, 0, 0 }, { 73, ',', 0, 0 }, { 74, '\'', 0x2018, 0x2019 },
        { 75, '?', 0, 0 }, { 76, 0x2605, '*', 0 }, { 77, 0x2606, 0, 0 }, { 78, '"', 0x201C, 0 }, { 79, 0x201D, 0, 0 },
        { 80, 0x2026, 0, 0 }, { 81, '/', 0, 0 }, { 82, '&', 0, 0 },
    };
    s_count = 0;
    uint8_t px[GH][GW];
    for (int i = 0; i < 10; i++) { rom_glyph(bank, i, px); add_glyph('0' + i, px); }
    for (int i = 0; i < 26; i++) { rom_glyph(bank, 10 + i, px); add_glyph('A' + i, px); }
    for (int i = 0; i < 26; i++) { rom_glyph(bank, 36 + i, px); add_glyph('a' + i, px); }
    for (size_t m = 0; m < sizeof(map) / sizeof(map[0]); m++) {
        rom_glyph(bank, map[m].index, px);
        add_glyph(map[m].cp, px);
        if (map[m].cp2) add_glyph(map[m].cp2, px);
        if (map[m].cp3) add_glyph(map[m].cp3, px);
    }
    // sanity: the letters must have ink, or this isn't the font
    int inked = 0;
    for (int i = 0; i < 62; i++) { int lo, hi; if (bounds_x(s_glyphs[i].px, &lo, &hi)) inked++; }
    if (inked < 60) return fail("the glyphs read from the ROM are empty: is it a US Super Mario 64 ROM?");
    for (const char *c = " -:;+=_|[]{}<>#\\^`@"; *c; c++) made(*c);
    qsort(s_glyphs, s_count, sizeof(Glyph), cmp_glyph);
    return 1;
}

// ---- the SWF ----------------------------------------------------------------------------------------------------
typedef struct { uint8_t *d; size_t n, cap; } Buf;
static void put(Buf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 256; b->d = realloc(b->d, b->cap); }
    memcpy(b->d + b->n, p, n);
    b->n += n;
}
static void put8(Buf *b, unsigned v) { uint8_t x = (uint8_t)v; put(b, &x, 1); }
static void put16(Buf *b, unsigned v) { put8(b, v & 255); put8(b, (v >> 8) & 255); }
static void put32(Buf *b, uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }

typedef struct { Buf *out; uint32_t acc; int n; } Bits;
static void bits_u(Bits *b, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        b->acc = (b->acc << 1) | ((v >> i) & 1);
        if (++b->n == 8) { put8(b->out, b->acc); b->acc = 0; b->n = 0; }
    }
}
static void bits_s(Bits *b, int v, int n) { bits_u(b, (uint32_t)v & (n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1)), n); }
static void bits_flush(Bits *b) { if (b->n) { put8(b->out, b->acc << (8 - b->n)); b->acc = 0; b->n = 0; } }

static int sbits(int a, int b, int c, int d)
{
    int mn = a, mx = a;
    int v[3] = { b, c, d };
    for (int i = 0; i < 3; i++) { if (v[i] < mn) mn = v[i]; if (v[i] > mx) mx = v[i]; }
    int n = 1;
    while (!(-(1 << (n - 1)) <= mn && mx <= (1 << (n - 1)) - 1)) n++;
    return n < 2 ? 2 : n;
}

static void write_rect(Buf *out, int xmin, int xmax, int ymin, int ymax)
{
    Bits b = { out, 0, 0 };
    int n = sbits(xmin, xmax, ymin, ymax);
    bits_u(&b, n, 5);
    bits_s(&b, xmin, n); bits_s(&b, xmax, n); bits_s(&b, ymin, n); bits_s(&b, ymax, n);
    bits_flush(&b);
}

typedef struct { int x0, y0, x1, y1; } R;   // in glyph pixels, [x0,x1) [y0,y1)

// a glyph's pixels as the fewest simple rectangles: a run along a row, grown downward while the rows below have it too
static int rects(const uint8_t px[GH][GW], R *out)
{
    uint8_t used[GH][GW];
    memset(used, 0, sizeof(used));
    int n = 0;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            if (!px[y][x] || used[y][x]) continue;
            int x1 = x;
            while (x1 + 1 < GW && px[y][x1 + 1] && !used[y][x1 + 1]) x1++;
            int y1 = y;
            for (;;) {
                if (y1 + 1 >= GH) break;
                int all = 1;
                for (int k = x; k <= x1; k++) if (!px[y1 + 1][k] || used[y1 + 1][k]) { all = 0; break; }
                if (!all) break;
                y1++;
            }
            for (int yy = y; yy <= y1; yy++) for (int xx = x; xx <= x1; xx++) used[yy][xx] = 1;
            out[n++] = (R){ x, y, x1 + 1, y1 + 1 };
        }
    return n;
}

// a glyph's shape record: every rectangle a closed path, clockwise on screen (the interior on the right of the edges,
// fill style 1)
static void write_shape(Buf *out, const R *r, int n)
{
    Bits b = { out, 0, 0 };
    bits_u(&b, 1, 4);   // fill bits
    bits_u(&b, 0, 4);   // line bits
    for (int i = 0; i < n; i++) {
        int x0 = r[i].x0 * UNIT, y0 = (r[i].y0 - BASELINE) * UNIT, x1 = r[i].x1 * UNIT, y1 = (r[i].y1 - BASELINE) * UNIT;
        int nb = sbits(x0, y0, x0, y0);
        bits_u(&b, 0, 1);                 // a style change
        bits_u(&b, 0, 1); bits_u(&b, 0, 1);   // no new styles, no line style
        bits_u(&b, i == 0, 1);            // fill style 1 set (the first time)
        bits_u(&b, 0, 1);                 // fill style 0 unused
        bits_u(&b, 1, 1);                 // move to
        bits_u(&b, nb, 5); bits_s(&b, x0, nb); bits_s(&b, y0, nb);
        if (i == 0) bits_u(&b, 1, 1);     // fill style 1 = 1
        int dx[4] = { x1 - x0, 0, -(x1 - x0), 0 }, dy[4] = { 0, y1 - y0, 0, -(y1 - y0) };
        for (int e = 0; e < 4; e++) {
            int len = dx[e] ? dx[e] : dy[e];
            int ne = sbits(len, 0, 0, 0);
            bits_u(&b, 1, 1); bits_u(&b, 1, 1);         // a straight edge
            bits_u(&b, ne - 2, 4);
            if (dx[e]) { bits_u(&b, 0, 1); bits_u(&b, 0, 1); bits_s(&b, dx[e], ne); }   // horizontal: not general, not vertical
            else { bits_u(&b, 0, 1); bits_u(&b, 1, 1); bits_s(&b, dy[e], ne); }         // vertical
        }
    }
    bits_u(&b, 0, 6);   // end of shape
    bits_flush(&b);
}

static void tag(Buf *out, int code, const Buf *body)
{
    if (body->n < 63) put16(out, (code << 6) | (unsigned)body->n);
    else { put16(out, (code << 6) | 0x3F); put32(out, (uint32_t)body->n); }
    put(out, body->d, body->n);
}

static int write_swf(const char *path, const char *name)
{
    int n = s_count;
    Buf shapes = { 0 }, glyphBody = { 0 };
    uint32_t *offs = calloc(n + 1, sizeof(uint32_t));
    int *bounds = calloc(n * 4, sizeof(int));
    for (int i = 0; i < n; i++) {
        R r[GW * GH];
        int nr = rects(s_glyphs[i].px, r);
        offs[i] = (uint32_t)(4 * (n + 1) + shapes.n);
        Buf sh = { 0 };
        write_shape(&sh, r, nr);
        put(&shapes, sh.d, sh.n);
        free(sh.d);
        int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        for (int k = 0; k < nr; k++) {
            int ax = r[k].x0 * UNIT, bx = r[k].x1 * UNIT, ay = (r[k].y0 - BASELINE) * UNIT, by = (r[k].y1 - BASELINE) * UNIT;
            if (k == 0 || ax < x0) x0 = ax;
            if (k == 0 || bx > x1) x1 = bx;
            if (k == 0 || ay < y0) y0 = ay;
            if (k == 0 || by > y1) y1 = by;
        }
        bounds[i * 4] = x0; bounds[i * 4 + 1] = x1; bounds[i * 4 + 2] = y0; bounds[i * 4 + 3] = y1;
    }
    offs[n] = (uint32_t)(4 * (n + 1) + shapes.n);   // the code table
    size_t nl = strlen(name);

    put16(&glyphBody, 1);                       // font id
    put8(&glyphBody, 0x80 | 0x08 | 0x04);       // layout, wide offsets, wide codes
    put8(&glyphBody, 1);                        // Latin
    put8(&glyphBody, (unsigned)nl);
    put(&glyphBody, name, nl);
    put16(&glyphBody, (unsigned)n);
    for (int i = 0; i <= n; i++) put32(&glyphBody, offs[i]);
    put(&glyphBody, shapes.d, shapes.n);
    for (int i = 0; i < n; i++) put16(&glyphBody, (unsigned)s_glyphs[i].cp);
    put16(&glyphBody, BASELINE * UNIT);         // ascent
    put16(&glyphBody, (GH - BASELINE) * UNIT);  // descent
    put16(&glyphBody, 0);                       // leading
    for (int i = 0; i < n; i++) put16(&glyphBody, (unsigned)(s_glyphs[i].adv * UNIT));
    for (int i = 0; i < n; i++) write_rect(&glyphBody, bounds[i * 4], bounds[i * 4 + 1], bounds[i * 4 + 2], bounds[i * 4 + 3]);
    put16(&glyphBody, 0);                       // no kerning

    Buf tags = { 0 }, t = { 0 };
    put32(&t, 0); tag(&tags, 69, &t); t.n = 0;                         // FileAttributes
    tag(&tags, 75, &glyphBody);                                         // DefineFont3
    put16(&t, 1); put(&t, name, nl); put8(&t, 0); put8(&t, 0); tag(&tags, 88, &t); t.n = 0;   // DefineFontName
    put16(&t, 1); put16(&t, 1); put(&t, name, nl); put8(&t, 0); tag(&tags, 56, &t); t.n = 0;  // ExportAssets
    put16(&tags, (1 << 6) | 0);                                         // ShowFrame
    put16(&tags, 0);                                                    // End

    Buf data = { 0 };
    write_rect(&data, 0, 11000, 0, 8000);
    put16(&data, 24 << 8);
    put16(&data, 1);
    put(&data, tags.d, tags.n);

    Buf file = { 0 };
    put(&file, "FWS", 3);
    put8(&file, 10);
    put32(&file, (uint32_t)(8 + data.n));
    put(&file, data.d, data.n);

    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(file.d, 1, file.n, f) == file.n;
    if (f) fclose(f);
    free(shapes.d); free(glyphBody.d); free(tags.d); free(t.d); free(data.d); free(file.d); free(offs); free(bounds);
    return ok ? 1 : fail("couldn't write the font file");
}

// rom: a big-endian (.z64) US Super Mario 64 ROM. 1 on success; else 0 and fontgen_error()
int fontgen_make(const uint8_t *rom, size_t len, const char *outPath)
{
    s_error[0] = 0;
    uint32_t size = 0;
    uint8_t *bank = mio0_decode(rom, len, BANK_ROM, &size);
    if (!bank) return 0;
    if (size < FONT_OFFSET + FONT_GLYPHS * 64) { free(bank); return fail("the ROM's graphics bank is too small"); }
    int ok = build_glyphs(bank);
    free(bank);
    return ok && write_swf(outPath, "Mario64");
}
