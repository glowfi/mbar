/* mbar configuration. copied to config.h on first build; edit config.h. */

/* appearance: gruvbox dark */
#define BARHEIGHT 28
#define ICONSCALE 1.0             /* fallback icon/emoji font size multiplier */
#define FONTPX    15.5            /* font pixel height                       */
#define PAD       10              /* bar left/right padding                  */
#define TAGPAD    8               /* horizontal padding inside a tag         */
#define SEP       "  |  "
#define BG        hex("#282828")
#define FG        hex("#ebdbb2")
#define ACTIVE    hex("#d79921")
#define ACTIVE_FG hex("#282828")
#define URGENT    hex("#fb4934")
#define DIM       hex("#928374")

#ifndef BITMAP_FONT
/* primary font: first that exists wins; override: MBAR_FONT=/path/font.ttf */
static const char *fontpaths[] = {
	"/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
	"/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
	"/usr/share/fonts/TTF/Hack-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
	"/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
};
/* fallbacks: codepoints missing from the primary are looked up here.
 * monochrome fonts only (nerd icons, NotoEmoji) — color emoji fonts
 * (NotoColorEmoji) store PNGs which stb_truetype cannot render. */
static const char *fallbackpaths[] = {
	"/usr/share/fonts/TTF/FantasqueSansMNerdFont-Regular.ttf",
	"/usr/share/fonts/joypixels/JoyPixels.ttf",
	"/usr/share/fonts/TTF/SymbolsNerdFont-Regular.ttf",
	"/usr/share/fonts/TTF/SymbolsNerdFontMono-Regular.ttf",
	"/usr/share/fonts/noto/NotoEmoji-Regular.ttf",
	"/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
};
#endif

#ifdef BITMAP_FONT
#define FSCALE 2               /* 8px pixel font drawn at FSCALE x */
#endif

/* ----------------------------- blocks -----------------------------------
 * a block is a function that writes a string into o. blocks are drawn
 * left to right, joined by SEP. for external
 * scripts use blk_cmd(cmd, seconds, &cache, o, n): runs cmd every
 * `seconds`, serves the cached first line of output in between. */
static void blk_date(char *o, size_t n) {
	time_t t = time(NULL);
	strftime(o, n, "%a %d %b %Y", localtime(&t));
}
static void blk_time(char *o, size_t n) {
	time_t t = time(NULL);
	strftime(o, n, "%H:%M:%S", localtime(&t));
}
static void blk_traffic(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("networkTraffic.sh", 1, &c, o, n);
}
static void blk_resources(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("resources.sh", 1, &c, o, n);
}
static void blk_network(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("network.sh", 1, &c, o, n);
}
static void blk_blue(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("bluestatus.sh", 1, &c, o, n);
}
static void blk_volume(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("volume.sh", 1, &c, o, n);
}
static void blk_brightness(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("brightness.sh", 1, &c, o, n);
}
static void blk_battery(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("battery.sh", 1, &c, o, n);
}
static void blk_timedate(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("time_date.sh", 1, &c, o, n);
}
static void blk_layout(char *o, size_t n) {
	static struct cmdcache c;
	blk_cmd("layout.sh", 1, &c, o, n);
}
/* right side, dwmblocks style: add a function above, list it here. */
static void (*blocks[])(char *, size_t) = {
	blk_layout, blk_traffic, blk_resources, blk_network, blk_blue,
	blk_volume,  blk_brightness, blk_battery, blk_timedate,
};

