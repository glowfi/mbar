/* mbar — minimal status bar for mango. tags left, date+time right.
 * deps: libwayland-client only. text: real TTF, rasterized with the
 * public-domain single-header stb_truetype (anti-aliased). */
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

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#include "ext-workspace-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* ------------------------- config: gruvbox dark ------------------------- */
#define BARHEIGHT 28
#define FONTPX    15.0            /* font pixel height                       */
#define PAD       10              /* bar left/right padding                  */
#define TAGPAD    8               /* horizontal padding inside a tag         */
#define SEP       "  |  "
#define BG        0xff282828      /* bg0                                     */
#define FG        0xffebdbb2      /* fg1                                     */
#define ACTIVE    0xffd79921      /* yellow: tag you are on (block)          */
#define ACTIVE_FG 0xff282828      /* text on the active block                */
#define URGENT    0xfffb4934      /* bright red                              */
#define DIM       0xff928374      /* gray: hidden/empty tag                  */

/* first font that exists wins; override with MBAR_FONT=/path/to/font.ttf */
static const char *fontpaths[] = {
	"/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
	"/usr/share/fonts/TTF/Hack-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
	"/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
};

static void blk_date(char *o, size_t n) {
	time_t t = time(NULL);
	strftime(o, n, "%a %d %b %Y", localtime(&t));
}
static void blk_time(char *o, size_t n) {
	time_t t = time(NULL);
	strftime(o, n, "%H:%M:%S", localtime(&t));
}
/* right side, dwmblocks style: add a function above, list it here. */
static void (*blocks[])(char *, size_t) = { blk_date, blk_time };
/* ------------------------------------------------------------------------ */

#define MAXWS   32
#define NAMELEN 32
#define LEN(a)  (sizeof(a) / sizeof(*(a)))

struct ws {
	struct ext_workspace_handle_v1 *h;
	char name[NAMELEN];
	uint32_t state;
};
struct glyph { unsigned char *bm; int w, h, xo, yo, adv; };

static struct wl_display *dpy;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct ext_workspace_manager_v1 *ws_mgr;
static struct wl_surface *surf;
static struct zwlr_layer_surface_v1 *layer_surf;
static struct ws wss[MAXWS];
static struct glyph glyphs[95];        /* ascii 32..126 */
static int font_asc, font_desc;
static int nws, width, height = BARHEIGHT, running = 1, dirty;

/* ------------------------------- font ---------------------------------- */
static void init_font(void) {
	unsigned char *data = NULL;
	const char *env = getenv("MBAR_FONT"), *used = NULL;
	for (size_t i = 0; i < LEN(fontpaths) + 1; i++) {
		const char *p = i == 0 ? env : fontpaths[i - 1];
		FILE *f = p ? fopen(p, "rb") : NULL;
		if (!f) continue;
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		data = malloc(sz);
		if (fread(data, 1, sz, f) != (size_t)sz) exit(1);
		fclose(f);
		used = p;
		break;
	}
	if (!data) {
		fputs("mbar: no font found, set MBAR_FONT=/path/to/font.ttf\n", stderr);
		exit(1);
	}
	fprintf(stderr, "mbar: font %s\n", used);
	stbtt_fontinfo f;
	stbtt_InitFont(&f, data, stbtt_GetFontOffsetForIndex(data, 0));
	float s = stbtt_ScaleForPixelHeight(&f, FONTPX);
	int a, d, l;
	stbtt_GetFontVMetrics(&f, &a, &d, &l);
	font_asc = a * s + 0.5;
	font_desc = -d * s + 0.5;
	for (int c = 32; c < 127; c++) {
		struct glyph *g = &glyphs[c - 32];
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&f, c, &adv, &lsb);
		g->adv = adv * s + 0.5;
		g->bm = stbtt_GetCodepointBitmap(&f, s, s, c, &g->w, &g->h,
		                                 &g->xo, &g->yo);
	}
}

static uint32_t blend(uint32_t dst, uint32_t fg, unsigned a) {
	unsigned r = ((fg >> 16 & 0xff) * a + (dst >> 16 & 0xff) * (255 - a)) / 255;
	unsigned g = ((fg >> 8 & 0xff) * a + (dst >> 8 & 0xff) * (255 - a)) / 255;
	unsigned b = ((fg & 0xff) * a + (dst & 0xff) * (255 - a)) / 255;
	return 0xff000000 | r << 16 | g << 8 | b;
}
static int draw_str(uint32_t *px, const char *s, int x, int base, uint32_t fg) {
	for (; *s; s++) {
		if (*s < 32 || *s > 126) continue;
		struct glyph *g = &glyphs[*s - 32];
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
	for (; *s; s++)
		if (*s >= 32 && *s <= 126) w += glyphs[*s - 32].adv;
	return w;
}
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
