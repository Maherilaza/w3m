# w3m

**A text-based web browser and pager.** w3m renders web pages in your
terminal and doubles as a fast local file pager.

This is the Debian-maintained fork of
[w3m](https://sourceforge.net/projects/w3m/), updated with JavaScript
support and modern keybindings.

## What's new in this fork

- **JavaScript support** — an embedded [QuickJS](https://bellard.org/quickjs/)
  engine runs page scripts during loading, so pages that write their
  content with JavaScript now render.
- **Render JS-heavy sites** — press `X` to re-render the current page
  through a real headless browser and swap the result in (press `X`
  again to switch back). Point the `js_renderer` siteconf option at the
  `js-renderer` helper script shipped with w3m; see
  [doc/README.siteconf](doc/README.siteconf). This makes single-page
  apps like crates.io readable.
- **Browser-style history** — `H` goes back and `L` goes forward
  without closing the current page, like a graphical browser.
- **Vim-like movement** — `b`/`e`/`w`, `W`/`B`/`E` word motions,
  `C-d`/`C-u` half-page and `C-f`/`C-b` full-page scrolling, `0` to the
  beginning of the line (default keymap only; the lynx keymap is
  unchanged).

## Features

- Renders tables, frames, colors, and forms in the terminal
- Inline images (via an external image viewer such as imlib2)
- Tabs, bookmarks, and a download list
- HTTPS via OpenSSL; cookies, proxies, and basic authentication
- Reads local files and pipes: `w3m file.html`, `man -l page.1 | w3m -T text/html`

## Quick start

Debian/Ubuntu dependencies:

```sh
sudo apt install build-essential libgc-dev libgpm-dev libssl-dev libimlib2-dev gettext
```

Build and install:

```sh
./configure
make
sudo make install
```

Note: without `libssl-dev`, configure silently disables SSL and
`https://` URLs will not work.

## Keys

| Key       | Action                                            |
| --------- | ------------------------------------------------- |
| `X`       | Render the page with an external JavaScript renderer (toggle) |
| `H` / `L` | Back / forward in history                          |
| `b` `e` `w` `W` `B` `E` | Word and big-word movement          |
| `C-d` / `C-u` | Scroll half a page down / up                 |
| `h j k l`, `gg`, `G` | Vim-style movement                    |

The full command list is in [doc/README.func](doc/README.func); the HELP
command (available from the menu) shows an interactive key summary.

## Documentation

- English: [doc/](doc/)
- Japanese: [doc-jp/](doc-jp/)

## License

Copyright (C) 1994-2002 Akinori Ito; (C) 2002-2011 Akinori Ito,
Hironori Sakamoto, Fumitoshi Ukai. Use, modification and redistribution
of this software is hereby granted, provided that this entire copyright
notice is included on any copies of this software and applications and
derivations thereof. This software is provided "as is" without warranty
of any kind. See [doc/README](doc/README) for details.

This package is maintained for [Debian](https://www.debian.org).
