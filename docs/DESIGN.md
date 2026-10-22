# Cairn design system

Shared by the client menu and the injector (`src/shared/UiTheme.h`, `Widgets.h`, `Brand.cpp`). The asset generator
(`tools/gen_assets.py`) uses the same values.

## Name and mark

A cairn is a small stack of stones that marks a trail: a mark left by someone who went first, built to be found again.
That is what this project is: a marker you put into a game you already run. The logo is three stacked rounded stones
(slate base, teal middle, amber top). It reads at 16 px because it is only three shapes, and it stays legible on dark
surfaces without any glow. The injector icon adds a small teal download badge.

The name is "Cairn" in sentence case in the UI, lower-case `cairn` in the wordmark.

## Palette

| Token | Hex | Use |
|---|---|---|
| `Bg` | `#101415` | window background |
| `Sidebar` | `#0C1011` | sidebar, title bar, status bar |
| `Surface` | `#161C1E` | cards, fields |
| `SurfaceHover` | `#1B2326` | hover |
| `SurfaceRaised` | `#212B2E` | popups, raised controls |
| `Border` | `#263033` | separators, card outlines |
| `BorderStrong` | `#354347` | hovered outlines, window border |
| `Text` | `#E7EEEC` | primary text |
| `TextDim` | `#8FA09D` | secondary text |
| `TextFaint` | `#5E6F6C` | tertiary text, idle icons |
| `Disabled` | `#4B5856` | disabled |
| `Accent` | `#3DB7A3` | primary accent (teal): toggles, selection, primary buttons |
| `AccentHover` | `#5CCBB8` | hover on accent |
| `Active` | `#2E9786` | pressed |
| `AccentText` | `#06201B` | text on accent |
| `Selected` | teal @ 16 % | selected row / card wash |
| `Accent2` | `#E8B45A` | secondary accent (amber): favourites, selected-card rail, experimental notes, "starting" |
| `Accent2Hover` | `#F0C57A` | hover on amber |
| `Success` / `Warning` / `Danger` | `#7BC96F` / `#E3A04B` / `#E5594F` | status |

Rules: one primary accent per screen area, amber only as a secondary cue, no gradients, no glow, 1 px borders, 8-14 px
rounding, animation limited to short hover/selection eases.

## Typography

Segoe UI (Windows font folder) with ImGui's default as fallback. Regular for body, Semibold for names and headings, a
0.8x "small" size for captions and log text, a 1.75x title size for the sidebar brand. Fonts are rebuilt at
`16 px x DPI scale` in the injector so text stays sharp when the DPI changes.

## Client menu

Sidebar (brand, navigation with live counts, categories, profile card), search + filter chips, module-card grid
(toggle, favourite star, settings chip; enabled cards get a teal rail, the selected card an amber rail), detail panel
with maturity badge and notes, Settings page, status bar. Empty states: "Starting up", "No modules loaded", "No modules
match". Nothing was removed.

## Injector

A single quiet column, 500 logical px wide. Top to bottom:

1. **Title bar** (graphite, no divider): small mark, "Cairn Injector", minimise and close. No subtitle or version.
2. **Minecraft**: label with a *Refresh* text button, then a flat list of instance rows (radio, window title, `javaw.exe - PID n`,
   version tag if the title has one). The list is a fixed 2-row area that scrolls, so the layout never jumps.
3. **Release**: the release source on one line, SHA-256 state on the next, *Open config folder* as a text button.
4. **Primary action**: one 44 px button, `Download & Inject`. Looks: ready (teal), hover, pressed, disabled (muted), working
   (spinner + current stage), success (teal outline, check, "Cairn running"), error (red tint, "Try again").
   A 3 px progress line sits under it and is the only progress indicator.
5. **Status**: one icon + one or two lines (teal check, red cross, amber warning, spinner, or neutral). Cancel appears here
   while cancellable.
6. **Details** (collapsed): process, path, version note, release URL, full hash, config/cache paths, and the activity log. The
   window grows to fit it.

Colour use: teal = the main action, the selected radio, and confirmed positive states; amber = warnings only; red = errors.
No cards around individual steps, no per-stage checklist, no gradients or glow, one 1 px window border.

## Not verified visually

The layouts were written without being run or screenshotted. The injector's row heights, the 2-row list, the fixed status area and the
height the window computes for itself all need a first look on a real screen.
