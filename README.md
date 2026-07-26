# mbar — minimal C status bar for mango (gruvbox dark)

~300 lines of C, links against **libwayland-client only** (plus libm).
No GTK/cairo/pango/freetype: text is anti-aliased TTF rendered by the
public-domain single-header stb_truetype, straight into a shm buffer.

- Left: workspace tags via `ext_workspace_manager_v1` (what mango exposes).
  Current tag = yellow gruvbox block, urgent = red, hidden = gray.
- Right: date + time, dwmblocks-style block functions.
- Docks to the top and reserves space via layer-shell.

## Build (Arch)

```sh
sudo pacman -S --needed wayland gcc make
make
./mbar
```

Protocol XMLs are vendored in `protocols/`. `sudo make install` → /usr/local/bin.
Autostart from your mango config with a plain `mbar` exec line.

## Font

At startup mbar picks the first font that exists from the `fontpaths[]` list
in bar.c (JetBrainsMono, Hack, DejaVu, Liberation, Noto...). Override any
time without recompiling:

```sh
MBAR_FONT=/usr/share/fonts/TTF/YourFont.ttf mbar
```

It prints which font it loaded on stderr. Size is `FONTPX` in the config
block (15px default), bar height is `BARHEIGHT` (28).

## Theme

Gruvbox dark out of the box; all colors are 0xAARRGGBB `#define`s at the
top of bar.c:

```
BG 282828  FG ebdbb2  ACTIVE d79921  URGENT fb4934  DIM 928374
```

Prefer a blue or aqua active block? Swap ACTIVE for 0xff458588 / 0xff689d6a.

## Add a block (the dwmblocks part)

```c
static void blk_bat(char *o, size_t n) {
    FILE *f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    int c = 0; if (f) { fscanf(f, "%d", &c); fclose(f); }
    snprintf(o, n, "BAT %d%%", c);
}
static void (*blocks[])(char *, size_t) = { blk_bat, blk_date, blk_time };
```

Blocks refresh once per second; a full redraw costs microseconds.

## Extending

- click-to-switch: bind `wl_seat`, on pointer press over a tag cell call
  `ext_workspace_handle_v1_activate(h)` + `ext_workspace_manager_v1_commit()`
- window title: listen to `zwlr_foreign_toplevel_manager_v1`, same pattern
