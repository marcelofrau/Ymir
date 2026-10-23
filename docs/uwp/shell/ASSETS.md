# Xbox shell — art asset contract

This is the contract between the artwork and the code of the Xbox shell. Any asset can be missing: the shell then
draws a procedural placeholder, so artwork can be added one file at a time.

## 1. Rules for every file

- **PNG (8-bit RGBA, straight, non-premultiplied alpha) or JPEG** for opaque images such as the backdrop.
- **Drawn for the 1920x1080 reference canvas.** A sphere that should look 140 px wide on a 1080p TV is drawn
  140 px wide (or larger, for a crisper 4K result: the shell scales it to its on-screen size). On a 4K display
  everything is scaled by 2.
- Keep the total under ~20 MB: everything is embedded in the executable.
- The visible content should not rely on the outer 5 % of the screen (TV overscan). Full-screen backgrounds may bleed
  to the edges; text and buttons may not.

## 2. Where the files go

| Location | Use |
|---|---|
| `apps/ymir-sdl3/uwp/shell/assets/` | Embedded in the app. Every `.png`/`.jpg`/`.toml` in this folder is picked up by CMake; re-run the configure step (`scripts/uwp/build.ps1 -Reconfigure`) after adding files |
| `<profile>/shell-theme/` on the console | **Overrides** the embedded files, including `theme.toml`. Upload with the Device Portal to try artwork without rebuilding; restart the app to reload |

The profile folder on the Xbox is
`LocalAppData\Ymir.SaturnEmulator.UWP_d7c8pgvss6ysm\LocalState\StrikerX3\Ymir\` (Device Portal → File explorer).

## 3. The manifest (`theme.toml`)

Each asset is an `[assets.<id>]` table:

```toml
[assets.sphere_disc]
file = "sphere_disc.png"   # required
frames = 32                # optional, sprite sheet frame count (default 1)
columns = 8                # optional, frames per row (default = frames, i.e. one row)
fps = 24                   # optional, animation speed (default 0 = static)

[assets.panel_frame]
file = "panel_frame.png"
slice = [64, 48, 64, 48]   # optional, 9-slice margins in pixels: left, top, right, bottom
```

- **Sprite sheets** are read left to right, top to bottom, all frames the same size.
- **9-slice**: the four corners keep their size, the edges stretch along one axis and the center along both. Margins
  are in pixels of the PNG and are scaled with the layout.

## 4. Asset ids

### 4.1 In use (embedded)

| Id | File | Size | Notes |
|---|---|---|---|
| `bg_space` | `background_nebula.jpg` | 1920x1080, opaque | Stars and the spiral galaxy. Drawn first, stretched over the whole display; the procedural starfield goes over it, faint while cruising and in full during warp jumps |
| `dash_bottom` | `dashboard_bottom.png` | 1920x405, alpha | The cockpit with the two pods. Placed at `layout.dash_bottom` |
| `dash_top` | `dashboard_top.png` | 1331x158, alpha | The top bar. Placed at `layout.dash_top`; its cyan area holds the dot-matrix LCD (`layout.lcd`) |

These three files are copies of the ones in `assets/` at the repository root, where they were delivered.

### 4.2 Optional (procedural without them)

| Id | What | Suggested size | Notes |
|---|---|---|---|
| `sphere_start` | Start / Resume | 140x140 per frame | See §5 |
| `sphere_disc` | Load Disc | 140x140 per frame | |
| `sphere_settings` | Settings | 140x140 per frame | |
| `sphere_desktop` | Switch to the desktop interface | 140x140 per frame | |
| `sphere_quit` | Quit | 140x140 per frame | |
| `sphere_bios` | Select BIOS (missing BIOS screen) | 140x140 per frame | |
| `sphere_rescan` | Scan Again (missing BIOS screen) | 140x140 per frame | |
| `sphere_glow` | Focus halo behind the focused sphere | ~450x450 | Drawn at 1.6x the sphere radius, pulsing. Placeholder: a ring of Saturn "mesh" dots with orbiting pixel blocks |
| `pod_glass_left` / `pod_glass_right` | Glass of the pods, drawn over the audio-reactive cubes (phase A) | pod size | Not read yet; the cubes float over the pods without it |

Ids planned for later phases (not read yet): `sphere_games`, `sphere_resume`, `sphere_savestate`,
`sphere_loadstate`, `sphere_reset`, `sphere_home`, `cover_placeholder`, `overlay_frame`.

## 4.3 User files (not part of the theme)

| Folder in the profile | Content | Notes |
|---|---|---|
| `covers/` | Game covers, `<product number>.png/jpg` (e.g. `MK-81009.png`) or `<disc file name>.png/jpg` | Any size; scaled down to 256 px. Shown in the library grid |
| `music/` | Background music, `.ogg`, `.mp3`, `.flac` or `.wav` | **Your own files, never added to the repository or the package** (game music is copyrighted). Restart Ymir after adding tracks |

## 5. Positions (`[layout]`)

Every position is data in the `[layout]` section of `theme.toml`, in coordinates of the 1920x1080 reference canvas.
Missing entries keep the defaults, which match the embedded artwork.

| Key | Format | Default | What |
|---|---|---|---|
| `dash_top` | `[x, y]` | `[294, 12]` | Top-left corner of `dash_top` |
| `dash_bottom` | `[x, y]` | `[0, 675]` | Top-left corner of `dash_bottom` |
| `lcd` | `[x, y, w, h]` | `[412, 50, 1095, 75]` | Dot-matrix LCD inside the cyan bar: "YMIR", the version and the marquee |
| `lcd_pitch` | number | `5` | Distance between LCD dots. The title is drawn at double size when the LCD has at least 14 rows |
| `pod_left` / `pod_right` | `[cx, cy, r]` | `[183, 814, 112]` / `[1735, 814, 112]` | The pods of `dash_bottom`, where the cubes float (phase A) |
| `grid` | `[x, y, w, h]` | `[330, 272, 1260, 450]` | The library grid floating in the sky |
| `grid_columns` | number | `6` | Tiles per row |
| `spheres` | `[first x, last x, y, radius]` | `[660, 1260, 905, 34]` | Row of action spheres on the cockpit console |
| `focus_label` | `[x, y]` | `[960, 192]` | Center of the focused item name |
| `focus_description` | `[x, y]` | `[960, 228]` | Center of the focused item details |
| `message_panel` | `[x, y, w, h]` | `[330, 262, 1260, 400]` | Panels such as "BIOS NOT FOUND", over the grid area |
| `legend` | `[x, y]` | `[960, 1000]` | Center of the controller legend on the cockpit floor |

## 6. Spheres

The idle sphere is drawn at a 70 px radius (140 px across) and grows 22 % while focused.

- **Static**: one frame. The shell animates the focus with size, halo and pulse.
- **Animated**: a sprite sheet (for example 32 frames of the sphere rotating). The animation **plays only while
  the sphere is focused**, like the selected option in the Saturn BIOS; idle spheres show frame 0.

Either form works with the same id, so the choice can be made per sphere and changed later.

## 7. Placeholder colors

The procedural spheres use these colors; artwork does not have to match them.

| Option | Color |
|---|---|
| Start / Select BIOS | orange `#FF9628` |
| Load Disc / Scan Again | blue `#3C8CFF` |
| Settings | green `#46C86E` |
| Desktop UI | purple `#AA5AE6` |
| Quit | red `#E1323C` |
