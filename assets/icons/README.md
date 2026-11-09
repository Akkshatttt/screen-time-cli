# App icons

Everything here is generated from one image: `source/terminal-hourglass-1254.png`. Regenerate with
`python tools/make_icons.py` (needs Pillow). The Makefile embeds `ico/screen-time-cli.ico` into both exes via
`assets/app.rc` (`windres`), so the icon and the description show in Task Manager, Settings > Apps and File Explorer.

| Folder | What |
|---|---|
| `source/` | the original artwork (1254 px, dark navy rounded square, terminal window + hourglass) |
| `ico/screen-time-cli.ico` | all sizes in one file; this is the one embedded in the exes |
| `ico/by-size/screen-time-cli-<size>.ico` | one size per file, for anything that wants a single-size icon |
| `png/screen-time-cli-<size>.png` | the same frames as PNG, for docs or a web page |

## Which size is used where

| Size | Where Windows picks it |
|---|---|
| 16 | Task Manager rows, title bars, small lists, Settings > Apps list at 100% scale |
| 20, 24 | the same places at 125% / 150% display scaling, taskbar small buttons |
| 32, 40 | taskbar and Explorer list view at 100-125%, Task Manager at higher scaling |
| 48 | Explorer "medium icons" and the classic desktop icon |
| 64, 96 | high-DPI taskbar and Explorer at 150-200% scaling |
| 128 | Explorer "large icons" |
| 256 | Explorer "extra large icons" and previews; stored PNG-compressed |

The glyph fills only about 42% of the source canvas, so a plain downscale would be unreadable. Sizes up to 48 use a tighter centre crop of
the artwork (glyph fills 64-80% of the frame, with a light sharpen); larger sizes show progressively more of the full image. All frames get an
anti-aliased rounded-corner alpha mask so the corners are transparent on any background. Windows scales between the stored sizes itself when a
display needs one that is not listed.
