# Changelog

Notable changes to this fork, newest first. The top section is used as
the GitHub release notes by the release workflow.

## 2026.08.21 — release-2026.08.21-dbb294b

- Truecolor support: color options (`basic_color`, `anchor_color`,
  `visited_color`, `image_color`, `form_color`, `active_color`, `bg_color`,
  `mark_color`) accept `#RRGGBB` values, emitted as 24-bit SGR sequences;
  nearest-ANSI fallback when truecolor is unavailable (`W3M_NO_TRUECOLOR`
  forces the fallback). (#4)
- `X` (JS reload) falls back to the bundled `js-renderer` helper when
  `js_renderer` is not configured, instead of erroring out. (#5)
- `js-renderer` passes `--no-sandbox` automatically when running as
  root. (#5)

## 2026.08.21 — release-2026.08.21-72fba52

- Security fixes: out-of-bounds write from oversized colspan, bounded
  internal symbol and form values, restricted script fetching with size
  limits, hardened FTP PASV parsing, and cookie parsing fixes (expired
  cookie pruning, entity overflow, quote over-read). (#1)
- Publish per-platform binaries in releases. (#3)
- CI compatibility matrix: verify builds on older toolchains and
  arm64. (#2)
