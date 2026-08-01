/* mbar — minimal status bar for mango. tags left, blocks right.
 * deps: libwayland-client only. text: TTF via bundled stb_truetype,
 * utf-8 aware with font fallbacks (nerd font icons, monochrome emoji). */
#define _GNU_SOURCE
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

/* make BITMAP=1 builds with the embedded 8x8 pixel font (no ttf needed) */
#ifndef BITMAP_FONT
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"          /* decodes CBDT color-emoji PNGs */
#else
#include "font8x8_basic.h"
#endif
#include "ext-workspace-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "modules.h"           /* generated from modules/ by gen-modules.sh */

/* parse "#RRGGBB" or "#AARRGGBB" -> 0xAARRGGBB */
static uint32_t hex(const char *s) {
	uint32_t v = (uint32_t)strtoul(s + 1, NULL, 16);
	return strlen(s) == 9 ? v : 0xff000000 | v;
}

/* ----------------------------- command block ---------------------------- */
struct cmdcache { char out[128]; int tick; };

/* embedded module lookup: "weather.sh" -> its script text, else NULL */
static const char *module_text(const char *name) {
	for (int i = 0; modules[i].name; i++)
		if (!strcmp(modules[i].name, name)) return modules[i].text;
	return NULL;
}

/* run cmd every `secs` seconds, serve cached output in between.
 * if cmd names a file from modules/, the embedded copy is run. */
static void blk_cmd(const char *cmd, int secs, struct cmdcache *c,
                    char *o, size_t n) {
	if (c->tick-- <= 0) {
		c->tick = secs - 1;
		const char *m = module_text(cmd);
		FILE *p = popen(m ? m : cmd, "r");
		c->out[0] = '\0';
		if (p) {
			if (fgets(c->out, sizeof c->out, p))
				c->out[strcspn(c->out, "\n")] = '\0';
			pclose(p);
		}
	}
	snprintf(o, n, "%s", c->out);
}

#include "config.h"

#ifndef ICONSCALE
#define ICONSCALE 1.0   /* size multiplier for fallback (icon/emoji) fonts */
#endif

#define MAXWS    32
#define NAMELEN  32
#define MAXFONTS 8
#define GCACHE   512
#define LEN(a)   (sizeof(a) / sizeof(*(a)))

struct ws {
	struct ext_workspace_handle_v1 *h;
	char name[NAMELEN];
	uint32_t state;
};
#ifndef BITMAP_FONT
struct font {
	stbtt_fontinfo info;
	float scale;
	const unsigned char *data;
	uint32_t cblc, cbdt, cmap;   /* table offsets, 0 if absent */
	int coloronly;               /* CBDT-only font, stbtt can't init it */
};
struct glyph {
	uint32_t cp;
	unsigned char *bm;           /* outline: 8-bit alpha  */
	uint32_t *cbm;               /* color emoji: argb     */
	int color, w, h, xo, yo, adv;
};
#endif

static struct wl_display *dpy;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct ext_workspace_manager_v1 *ws_mgr;
static struct wl_surface *surf;
static struct zwlr_layer_surface_v1 *layer_surf;
static struct ws wss[MAXWS];
#ifndef BITMAP_FONT
static struct font fonts[MAXFONTS];    /* [0] primary, rest fallbacks */
static struct glyph gcache[GCACHE];    /* glyphs rasterized on demand */
static int nfonts, nglyphs, tfont;   /* tfont: first outline font */
#endif
static int font_asc, font_desc;
static int nws, width, height = BARHEIGHT, running = 1, dirty;

/* ------------------------------- font ---------------------------------- */
/* decode one utf-8 codepoint, return bytes consumed */
static int utf8_decode(const char *s, uint32_t *cp) {
	const unsigned char *u = (const unsigned char *)s;
	if (u[0] < 0x80) { *cp = u[0]; return 1; }
	if ((u[0] & 0xe0) == 0xc0 && u[1]) {
		*cp = (u[0] & 0x1f) << 6 | (u[1] & 0x3f);
		return 2;
	}
	if ((u[0] & 0xf0) == 0xe0 && u[1] && u[2]) {
		*cp = (u[0] & 0x0f) << 12 | (u[1] & 0x3f) << 6 | (u[2] & 0x3f);
		return 3;
	}
	if ((u[0] & 0xf8) == 0xf0 && u[1] && u[2] && u[3]) {
		*cp = (u[0] & 0x07) << 18 | (u[1] & 0x3f) << 12 |
		      (u[2] & 0x3f) << 6 | (u[3] & 0x3f);
		return 4;
	}
	*cp = '?';
	return 1;
}

#ifndef BITMAP_FONT
static int load_font(const char *path, int isfallback) {
	FILE *f = path ? fopen(path, "rb") : NULL;
	if (!f || nfonts == MAXFONTS) return 0;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *data = malloc(sz);
	if (fread(data, 1, sz, f) != (size_t)sz) exit(1);
	fclose(f);
	struct font *fo = &fonts[nfonts];
	uint32_t start = stbtt_GetFontOffsetForIndex(data, 0);
	fo->data = data;
	fo->cblc = stbtt__find_table(data, start, "CBLC");
	fo->cbdt = stbtt__find_table(data, start, "CBDT");
	fo->cmap = stbtt__find_table(data, start, "cmap");
	fo->coloronly = 0;
	if (!stbtt_InitFont(&fo->info, data, start)) {
		if (!(fo->cblc && fo->cbdt && fo->cmap)) {   /* truly unusable */
			free(data);
			return 0;
		}
		fo->coloronly = 1;   /* emoji font with no outlines (e.g. joypixels) */
	} else {
		/* fallbacks (icon fonts) size by em square: their line metrics
		 * are inflated, which made icons render tiny next to text */
		fo->scale = isfallback
		    ? stbtt_ScaleForMappingEmToPixels(&fo->info, FONTPX * ICONSCALE)
		    : stbtt_ScaleForPixelHeight(&fo->info, FONTPX);
	}
	fprintf(stderr, "mbar: font %s%s\n", path, fo->coloronly ? " (color emoji)" : "");
	return ++nfonts;
}

static int have_textfont(void) {
	for (int i = 0; i < nfonts; i++)
		if (!fonts[i].coloronly) return 1;
	return 0;
}
static void init_font(void) {
	load_font(getenv("MBAR_FONT"), 0);
	/* keep going down the list until we have a real text font: a color
	 * emoji font placed early (or as MBAR_FONT) must not become primary */
	for (size_t i = 0; i < LEN(fontpaths) && !have_textfont(); i++)
		load_font(fontpaths[i], 0);
	if (!have_textfont()) {
		fputs("mbar: no text font found, set MBAR_FONT=/path/to/font.ttf\n"
		      "mbar: (color emoji fonts cannot be the primary font)\n", stderr);
		exit(1);
	}
	for (size_t i = 0; i < LEN(fallbackpaths); i++)
		load_font(fallbackpaths[i], 1);
	while (fonts[tfont].coloronly) tfont++;
	int a, d, l;
	stbtt_GetFontVMetrics(&fonts[tfont].info, &a, &d, &l);
	font_asc = a * fonts[tfont].scale + 0.5;
	font_desc = -d * fonts[tfont].scale + 0.5;
}

/* ------------------- color emoji (CBDT/CBLC, e.g. joypixels) ------------ */
static uint32_t rd32(const unsigned char *p) {
	return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static uint16_t rd16(const unsigned char *p) { return p[0] << 8 | p[1]; }

/* minimal cmap lookup for fonts stbtt cannot init (format 4 and 12) */
static int cmap_gid(const unsigned char *d, uint32_t cmap, uint32_t cp) {
	uint32_t t = 0;
	int n = rd16(d + cmap + 2);
	for (int i = 0; i < n; i++) {
		const unsigned char *r = d + cmap + 4 + 8 * i;
		int pid = rd16(r), eid = rd16(r + 2);
		if (pid == 0 || (pid == 3 && (eid == 1 || eid == 10)))
			t = cmap + rd32(r + 4);
		if (pid == 3 && eid == 10) break;   /* full-unicode table: best */
	}
	if (!t) return 0;
	int fmt = rd16(d + t);
	if (fmt == 12) {
		uint32_t ng = rd32(d + t + 12);
		for (uint32_t i = 0; i < ng; i++) {
			const unsigned char *g = d + t + 16 + 12 * i;
			uint32_t s = rd32(g), e = rd32(g + 4);
			if (cp >= s && cp <= e) return rd32(g + 8) + (cp - s);
		}
	} else if (fmt == 4 && cp < 0x10000) {
		int sx2 = rd16(d + t + 6);
		const unsigned char *end = d + t + 14, *sta = end + sx2 + 2,
		                    *del = sta + sx2, *ran = del + sx2;
		for (int i = 0; i < sx2; i += 2) {
			if (cp > rd16(end + i)) continue;
			if (cp < rd16(sta + i)) return 0;
			int ro = rd16(ran + i);
			if (!ro) return (cp + (int16_t)rd16(del + i)) & 0xffff;
			uint32_t gi = rd16(ran + i + ro + (cp - rd16(sta + i)) * 2);
			return gi ? (gi + (int16_t)rd16(del + i)) & 0xffff : 0;
		}
	}
	return 0;
}

static int font_gid(struct font *f, uint32_t cp) {
	return f->coloronly ? cmap_gid(f->data, f->cmap, cp)
	                    : stbtt_FindGlyphIndex(&f->info, cp);
}

/* fetch glyph gid as a color bitmap from CBDT, scaled to the text height */
static int cbdt_load(struct font *f, int gid, struct glyph *g) {
	if (!f->cblc || !f->cbdt || gid <= 0) return 0;
	const unsigned char *d = f->data;
	int target = font_asc + font_desc;
	/* strike: smallest ppem >= target, else the largest available */
	uint32_t nsizes = rd32(d + f->cblc + 4), rec = 0;
	int best = 0;
	for (uint32_t i = 0; i < nsizes; i++) {
		uint32_t r = f->cblc + 8 + 48 * i;
		int ppem = d[r + 45];
		int better = best < target ? ppem > best
		                           : (ppem >= target && ppem < best);
		if (better) { best = ppem; rec = r; }
	}
	if (!rec) return 0;
	/* index subtable covering gid */
	uint32_t arr = f->cblc + rd32(d + rec), nsub = rd32(d + rec + 8);
	uint32_t sub = 0, first = 0;
	for (uint32_t j = 0; j < nsub; j++) {
		const unsigned char *e = d + arr + 8 * j;
		if ((uint32_t)gid >= rd16(e) && (uint32_t)gid <= rd16(e + 2)) {
			first = rd16(e);
			sub = arr + rd32(e + 4);
			break;
		}
	}
	if (!sub) return 0;
	int idxfmt = rd16(d + sub), imgfmt = rd16(d + sub + 2);
	uint32_t imgoff = rd32(d + sub + 4), off, len;
	if (idxfmt == 1) {
		const unsigned char *o = d + sub + 8 + 4 * (gid - first);
		off = rd32(o); len = rd32(o + 4) - off;
	} else if (idxfmt == 3) {
		const unsigned char *o = d + sub + 8 + 2 * (gid - first);
		off = rd16(o); len = rd16(o + 2) - off;
	} else return 0;
	if (!len) return 0;
	const unsigned char *p = d + f->cbdt + imgoff + off, *png;
	uint32_t plen;
	if      (imgfmt == 17) { png = p + 9;  plen = rd32(p + 5); }
	else if (imgfmt == 18) { png = p + 12; plen = rd32(p + 8); }
	else if (imgfmt == 19) { png = p + 4;  plen = rd32(p); }
	else return 0;
	int sw, sh, comp;
	unsigned char *rgba = stbi_load_from_memory(png, plen, &sw, &sh, &comp, 4);
	if (!rgba) return 0;
	/* box-average downscale to text height */
	int th = target, tw = sw * th / sh;
	if (tw < 1) tw = 1;
	uint32_t *out = malloc((size_t)tw * th * 4);
	for (int y = 0; y < th; y++)
		for (int x = 0; x < tw; x++) {
			int x0 = x * sw / tw, x1 = (x + 1) * sw / tw;
			int y0 = y * sh / th, y1 = (y + 1) * sh / th;
			if (x1 <= x0) x1 = x0 + 1;
			if (y1 <= y0) y1 = y0 + 1;
			unsigned r = 0, gc = 0, b = 0, a = 0, cnt = (x1 - x0) * (y1 - y0);
			for (int yy = y0; yy < y1; yy++)
				for (int xx = x0; xx < x1; xx++) {
					const unsigned char *s = rgba + 4 * (yy * sw + xx);
					r += s[0]; gc += s[1]; b += s[2]; a += s[3];
				}
			out[y * tw + x] = (a/cnt) << 24 | (r/cnt) << 16 | (gc/cnt) << 8 | (b/cnt);
		}
	stbi_image_free(rgba);
	g->color = 1;
	g->cbm = out;
	g->w = tw; g->h = th;
	g->xo = 0; g->yo = -font_asc;
	g->adv = tw + 1;
	return 1;
}


/* rasterize on first use, cache; first font containing the codepoint wins */
static struct glyph *get_glyph(uint32_t cp) {
	for (int i = 0; i < nglyphs; i++)
		if (gcache[i].cp == cp) return &gcache[i];
	if (nglyphs == GCACHE) nglyphs = 0;   /* wrap: crude, fine for a bar */
	struct glyph *g = &gcache[nglyphs++];
	free(g->bm);  g->bm = NULL;
	free(g->cbm); g->cbm = NULL;
	g->color = 0;
	g->cp = cp;
	for (int i = 0; i < nfonts; i++) {
		struct font *f = &fonts[i];
		int gid = font_gid(f, cp);
		if (!gid) continue;
		if (cbdt_load(f, gid, g))          /* color emoji png */
			return g;
		if (f->coloronly) continue;        /* claims cp, has no png: skip */
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
		g->adv = adv * f->scale + 0.5;
		g->bm = stbtt_GetCodepointBitmap(&f->info, f->scale, f->scale, cp,
		                                 &g->w, &g->h, &g->xo, &g->yo);
		return g;
	}
	/* no font has it: draw ? from the text font */
	struct font *f = &fonts[tfont];
	int adv, lsb;
	stbtt_GetCodepointHMetrics(&f->info, '?', &adv, &lsb);
	g->adv = adv * f->scale + 0.5;
	g->bm = stbtt_GetCodepointBitmap(&f->info, f->scale, f->scale, '?',
	                                 &g->w, &g->h, &g->xo, &g->yo);
	return g;
}


static uint32_t blend(uint32_t dst, uint32_t fg, unsigned a) {
	unsigned r = ((fg >> 16 & 0xff) * a + (dst >> 16 & 0xff) * (255 - a)) / 255;
	unsigned g = ((fg >> 8 & 0xff) * a + (dst >> 8 & 0xff) * (255 - a)) / 255;
	unsigned b = ((fg & 0xff) * a + (dst & 0xff) * (255 - a)) / 255;
	return 0xff000000 | r << 16 | g << 8 | b;
}
static int draw_str(uint32_t *px, const char *s, int x, int base, uint32_t fg) {
	uint32_t cp;
	while (*s) {
		s += utf8_decode(s, &cp);
		if (cp == 0xfe0f || cp == 0x200d) continue;   /* VS16, ZWJ */
		struct glyph *g = get_glyph(cp);
		if (g->color) {
			for (int y = 0; y < g->h; y++)
				for (int gx = 0; gx < g->w; gx++) {
					uint32_t sp = g->cbm[y * g->w + gx];
					unsigned a = sp >> 24;
					int X = x + g->xo + gx, Y = base + g->yo + y;
					if (a && X >= 0 && X < width && Y >= 0 && Y < height)
						px[Y * width + X] = blend(px[Y * width + X], sp, a);
				}
			x += g->adv;
			continue;
		}
		for (int y = 0; y < g->h; y++)
			for (int gx = 0; gx < g->w; gx++) {
				unsigned a = g->bm[y * g->w + gx];
				int X = x + g->xo + gx, Y = base + g->yo + y;
				if (a && X >= 0 && X < width && Y >= 0 && Y < height)
					px[Y * width + X] = blend(px[Y * width + X], fg, a);
			}
		x += g->adv;
	}
	return x;
}
static int text_w(const char *s) {
	int w = 0;
	uint32_t cp;
	while (*s) {
		s += utf8_decode(s, &cp);
		if (cp == 0xfe0f || cp == 0x200d) continue;
		w += get_glyph(cp)->adv;
	}
	return w;
}

#else /* BITMAP_FONT: embedded 8x8 pixel font, ascii only, no files */
static void init_font(void) {
	font_asc = 8 * FSCALE;
	font_desc = 0;
}
static int draw_str(uint32_t *px, const char *s, int x, int base, uint32_t fg) {
	int top = base - font_asc;
	uint32_t cp;
	while (*s) {
		s += utf8_decode(s, &cp);
		int c = cp > 127 ? '?' : (int)cp;
		for (int y = 0; y < 8; y++)
			for (int gx = 0; gx < 8; gx++) {
				if (!(font8x8_basic[c][y] & (1 << gx))) continue;
				for (int sy = 0; sy < FSCALE; sy++)
					for (int sx = 0; sx < FSCALE; sx++) {
						int X = x + gx * FSCALE + sx;
						int Y = top + y * FSCALE + sy;
						if (X >= 0 && X < width && Y >= 0 && Y < height)
							px[Y * width + X] = fg;
					}
			}
		x += 8 * FSCALE;
	}
	return x;
}
static int text_w(const char *s) {
	int w = 0;
	uint32_t cp;
	while (*s) {
		s += utf8_decode(s, &cp);
		w += 8 * FSCALE;
	}
	return w;
}
#endif /* BITMAP_FONT */
static void fill(uint32_t *px, int x0, int y0, int w, int h, uint32_t c) {
	for (int y = y0; y < y0 + h && y < height; y++)
		for (int x = x0; x < x0 + w && x < width; x++)
			if (x >= 0 && y >= 0) px[y * width + x] = c;
}

/* ----------------------------- rendering ------------------------------- */
static void buf_release(void *data, struct wl_buffer *b) {
	munmap(data, width * height * 4);
	wl_buffer_destroy(b);
}
static const struct wl_buffer_listener buf_lis = { buf_release };

static int ws_cmp(const void *a, const void *b) {
	const struct ws *x = a, *y = b;
	size_t lx = strlen(x->name), ly = strlen(y->name);
	return lx != ly ? (int)lx - (int)ly : strcmp(x->name, y->name);
}

static void draw(void) {
	if (!width) return;
	int stride = width * 4, size = stride * height;
	int fd = memfd_create("mbar", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) < 0) exit(1);
	uint32_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (px == MAP_FAILED) exit(1);

	fill(px, 0, 0, width, height, BG);
	int base = (height - (font_asc + font_desc)) / 2 + font_asc;

	/* left: tags */
	qsort(wss, nws, sizeof(*wss), ws_cmp);
	int x = PAD;
	for (int i = 0; i < nws; i++) {
		int cw = text_w(wss[i].name) + 2 * TAGPAD;
		if (wss[i].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) {
			fill(px, x, 0, cw, height, ACTIVE);
			draw_str(px, wss[i].name, x + TAGPAD, base, ACTIVE_FG);
		} else {
			uint32_t fg = wss[i].state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT
			    ? URGENT
			    : wss[i].state & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN ? DIM : FG;
			draw_str(px, wss[i].name, x + TAGPAD, base, fg);
		}
		x += cw;
	}

	/* right: blocks */
	char line[256] = "", b[64];
	for (size_t i = 0; i < LEN(blocks); i++) {
		blocks[i](b, sizeof b);
		if (i) strncat(line, SEP, sizeof line - strlen(line) - 1);
		strncat(line, b, sizeof line - strlen(line) - 1);
	}
	draw_str(px, line, width - PAD - text_w(line), base, FG);

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buf =
	    wl_shm_pool_create_buffer(pool, 0, width, height, stride,
	                              WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(buf, &buf_lis, px);
	wl_surface_attach(surf, buf, 0, 0);
	wl_surface_damage_buffer(surf, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surf);
	dirty = 0;
}

/* ------------------------ ext-workspace events -------------------------- */
static struct ws *find_ws(struct ext_workspace_handle_v1 *h) {
	for (int i = 0; i < nws; i++) if (wss[i].h == h) return &wss[i];
	return NULL;
}
static void ws_id(void *d, struct ext_workspace_handle_v1 *h, const char *s) {}
static void ws_name(void *d, struct ext_workspace_handle_v1 *h, const char *s) {
	struct ws *w = find_ws(h);
	if (w) snprintf(w->name, NAMELEN, "%s", s);
}
static void ws_coords(void *d, struct ext_workspace_handle_v1 *h,
                      struct wl_array *a) {}
static void ws_state(void *d, struct ext_workspace_handle_v1 *h, uint32_t st) {
	struct ws *w = find_ws(h);
	if (w) w->state = st;
}
static void ws_caps(void *d, struct ext_workspace_handle_v1 *h, uint32_t c) {}
static void ws_removed(void *d, struct ext_workspace_handle_v1 *h) {
	struct ws *w = find_ws(h);
	if (w) *w = wss[--nws];
	ext_workspace_handle_v1_destroy(h);
}
static const struct ext_workspace_handle_v1_listener ws_lis = {
	ws_id, ws_name, ws_coords, ws_state, ws_caps, ws_removed,
};

static void mgr_group(void *d, struct ext_workspace_manager_v1 *m,
                      struct ext_workspace_group_handle_v1 *g) {}
static void mgr_ws(void *d, struct ext_workspace_manager_v1 *m,
                   struct ext_workspace_handle_v1 *h) {
	if (nws < MAXWS) {
		wss[nws++] = (struct ws){ .h = h };
		ext_workspace_handle_v1_add_listener(h, &ws_lis, NULL);
	}
}
static void mgr_done(void *d, struct ext_workspace_manager_v1 *m) { dirty = 1; }
static void mgr_fin(void *d, struct ext_workspace_manager_v1 *m) {}
static const struct ext_workspace_manager_v1_listener mgr_lis = {
	mgr_group, mgr_ws, mgr_done, mgr_fin,
};

/* --------------------------- layer surface ------------------------------ */
static void ls_configure(void *d, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h) {
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (w) width = w;
	if (h) height = h;
	draw();
}
static void ls_closed(void *d, struct zwlr_layer_surface_v1 *ls) {
	running = 0;
}
static const struct zwlr_layer_surface_v1_listener ls_lis = {
	ls_configure, ls_closed,
};

/* ----------------------------- registry --------------------------------- */
static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver) {
	if (!strcmp(iface, wl_compositor_interface.name))
		comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
		layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
	else if (!strcmp(iface, ext_workspace_manager_v1_interface.name))
		ws_mgr = wl_registry_bind(r, name, &ext_workspace_manager_v1_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_lis = { reg_global, reg_remove };

/* ------------------------------- main ----------------------------------- */
int main(void) {
	init_font();
	if (!(dpy = wl_display_connect(NULL))) {
		fputs("mbar: cannot connect to wayland display\n", stderr);
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &reg_lis, NULL);
	wl_display_roundtrip(dpy);
	if (!comp || !shm || !layer_shell) {
		fputs("mbar: missing wl_compositor/wl_shm/layer_shell\n", stderr);
		return 1;
	}
	if (ws_mgr)
		ext_workspace_manager_v1_add_listener(ws_mgr, &mgr_lis, NULL);
	else
		fputs("mbar: no ext_workspace_manager_v1, tags disabled\n", stderr);

	surf = wl_compositor_create_surface(comp);
	layer_surf = zwlr_layer_shell_v1_get_layer_surface(
	    layer_shell, surf, NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "mbar");
	zwlr_layer_surface_v1_add_listener(layer_surf, &ls_lis, NULL);
	zwlr_layer_surface_v1_set_anchor(layer_surf,
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(layer_surf, 0, BARHEIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(layer_surf, BARHEIGHT);
	wl_surface_commit(surf);

	int fd = wl_display_get_fd(dpy);
	time_t last = 0;
	while (running) {
		while (wl_display_prepare_read(dpy) != 0)
			wl_display_dispatch_pending(dpy);
		wl_display_flush(dpy);

		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		int timeout = 1000 - ts.tv_nsec / 1000000 + 10; /* next second */

		struct pollfd p = { .fd = fd, .events = POLLIN };
		if (poll(&p, 1, timeout) > 0 && (p.revents & POLLIN)) {
			wl_display_read_events(dpy);
			wl_display_dispatch_pending(dpy);
		} else {
			wl_display_cancel_read(dpy);
		}
		time_t now = time(NULL);
		if (dirty || now != last) { last = now; draw(); }
	}
	return 0;
}
