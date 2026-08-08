# mbar

A status bar for [mango](https://github.com/mangowm/mango).

Depends on `libwayland-client`. Nothing else. Text, icons and color emoji
are rendered by bundled public-domain single-header libraries.

- workspace tags on the left, click to switch
- dwmblocks-style blocks on the right, click to run things
- shell modules embedded into the binary at build time
- blocks run in a worker thread, a slow script never lags the bar
- gruvbox dark

## build

    make
    sudo make install

`make BITMAP=1` builds with an embedded 8x8 pixel font instead of TTF.

## configure

Edit `config.h`, recompile. Like dwm.

```c
static const struct block blocks[] = {
	{ blk_volume,  "pavucontrol" },
	{ blk_network, "kitty nmtui" },
	{ blk_time,    NULL },
};
```

A block is a C function. For scripts, drop a file in `modules/` and use
`blk_cmd("volume.sh", 2, &c, o, n)` — it gets compiled into the binary
and runs every 2 seconds.

## license

MIT. `stb_truetype.h`, `stb_image.h`, `font8x8_basic.h` are public domain.
