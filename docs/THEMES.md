# RiftWii themes

Everything about creating and installing menu themes for RiftWii.

## How themes work

A theme is a folder in `sd:/riftwii/themes/`, for example
`sd:/riftwii/themes/Midnight/`. It must hold `theme.ini`. Everything else
is optional.

Settings > **Theme** steps through *Default* (RiftWii's own light look, no
files needed) and every folder in `sd:/riftwii/themes/` that has a
`theme.ini`, shown by the theme's `name` (or the folder name if it has
none). RiftWii offers to restart its menu to show the new theme. The choice
is saved as `theme = <folder>` in `sd:/riftwii/settings.txt`
(`theme = default` for the default).

## The theme.ini file

`theme.ini` is a plain text file split into sections. Comments start with
`#` or `;`.

Anything missing keeps the default value. Anything wrong is skipped and
RiftWii keeps the default for it; it never stops the menu. Each skipped
line is written to `sd:/riftwii/session.log` as a line starting `Theme:`.

### [theme]

Holds theme information:

- `name`: the name shown in Settings > Theme. If omitted, the folder name
  is shown.
- `author`: the author of the theme.

### [shape]

Controls the painted parts' shape:

- `corners`: corner roundness multiplier, from `0` (square corners) to `2`
  (twice as round). The default is `1`. A player can override it for the
  Channels view's tiles alone in Settings > Home > Channel corners
  (`channel_corners` in `settings.txt`): `theme` (the default) keeps this
  value, and `square`, `slight`, `round` and `rounder` use `0`, `0.5`, `1`
  and `2` in every theme.
- `gloss`: `yes` or `no`. A shine across the top half of buttons, tiles
  and option boxes. The default is `no`.
- `bar`: `dip` or `bump`. Home's bottom bar sinks in the middle with the
  clock in the dip and the date under it, as the Wii Menu's (`dip`, the
  default), or rises there with the clock above it (`bump`). A theme's own
  `bar.png` should have the same shape.
- `home_menu`: `wii` or `ios6`. The HOME Menu as the Wii's own (`wii`, the
  default: black bars, round pale buttons) or as iOS 6 drew things
  (`ios6`: glossy bars in the `bar` colour, dark linen between them,
  shaded buttons with a red Power off). Bookshelf uses `ios6`.
- `shelf_grain`: `yes` or `no`. Wood grain along Home's shelf (`yes`, as
  Bookshelf and Midnight), or a smooth shelf with a thin `accent` line along
  its front edge (`no`, the default). A theme's own `shelf.png` wins.

### [backdrop]

Controls backdrop stripes:

- `stripes`: `yes` or `no`. Sets whether thin horizontal stripes are drawn
  across the backdrop. The default is `yes`.

### [colors]

Colours are written as `#RRGGBB` or `#RRGGBBAA`, where `AA` is opacity
(`00` transparent to `FF` solid). If `AA` is omitted, the colour is fully
solid. Hex letters can be upper case or lower case.

The table below lists every colour key, its default value, and where it
shows:

| Key | Default | Where it shows |
| --- | --- | --- |
| `ink` | `#2E2E36` | Titles and labels |
| `ink_soft` | `#4A4A54` | Values, secondary text (lighter text level) |
| `ink_dim` | `#6A6A74` | Hints, footers (lighter text level) |
| `clock` | `#74747E` | Home's clock |
| `accent` | `#2FB6E9` | Highlights, outlines, the bar's curve |
| `accent_ink` | `#0E6488` | Text in the accent's colour (switched-on values) |
| `text_on_accent` | `#FFFFFF` | Text drawn on the accent colour |
| `warn` | `#B03A2E` | Warnings, errors |
| `card` | `#FFFFFF` | Tiles, buttons, panels |
| `card_edge` | `#CFCFD6` | Outline of tiles, buttons and panels |
| `card_edge_strong` | `#C4C4CE` | Round buttons' rims |
| `shadow` | `#28283C22` | Shadow under tiles and buttons |
| `glow` | `#2FB6E950` | Glow around the highlighted part |
| `glyph` | `#55555F` | Icons and arrows |
| `chip_on` | `#E3F5FC` | An option's value box, changed from the default |
| `chip_off` | `#F4F4F6` | An option's value box at its default |
| `chip_off_edge` | `#D0D0D8` | Its outline |
| `switch_off` | `#D4D4DB` | An On/Off switch when off |
| `bar` | `#DEDEE4` | Home's bottom bar, the banner screen's and the title bands (a shade under the backdrop) |
| `backdrop` | `#ECECEF` | Behind every screen |
| `backdrop_stripe` | `#E3E3E8` | The backdrop's thin stripes |
| `banner_stripe` | `#FFFFFF14` | Stripes over a game page's banner while it shows the game's colour (no box art on the card yet) |
| `banner_tint` | `#00000000` | Mixed into each game's own colour and over its blurred box art (a game page's banner, plain spines and backs on the shelf) by its alpha: `00` keeps the games' colours, `FF` gives every game this colour |
| `divider` | `#E8E8EE` | Between list rows |
| `scroll_track` | `#E6E6EC` | A list's scroll track |
| `scroll_thumb` | `#A8A8B4` | A list's scroll thumb |
| `badge` | `#ECECF1` | A tile's DISC/USB/SD label |
| `shelf` | `#F4F6F9` | The top of Home's shelf |
| `shelf_edge` | `#D6DDE6` | The front edge of Home's shelf |
| `pointer1` | `#3B8FD6` | Outline of player 1's pointer hand |
| `pointer2` | `#D64545` | Outline of player 2's pointer hand |
| `pointer3` | `#3FA34D` | Outline of player 3's pointer hand |
| `pointer4` | `#D9A21B` | Outline of player 4's pointer hand |

## Pictures

A theme may replace any of RiftWii's painted pictures with a PNG of the
same name in its folder.

The picture must be exactly the size listed, or it is skipped (and noted
in `session.log`). Sizes include a transparent margin where RiftWii paints
the shadow or glow, so keep the visible part inside it, centred.
Pictures ending in `_over` are the highlighted versions. Transparency
(PNG alpha) is used.

`background.png` replaces the whole backdrop (and the stripes).

| Picture | Size | What it is |
| --- | --- | --- |
| `background` | 640x480 | The whole screen behind the menu |
| `tile` / `tile_over` | 140x100 | A game tile on Home (names view) |
| `tile_empty` | 140x100 | An empty place on Home (Names and Channels) |
| `cover_tile` / `cover_tile_over` | 96x128 | A game tile with its cover (the cover is drawn on top) |
| `round_button` / `round_button_over` | 80x80 | Home's two round buttons |
| `pill` / `pill_over` | 252x60 | The wide buttons (Back, OK) |
| `pill_primary` / `pill_primary_over` | 252x60 | The main wide button (Start) |
| `home_button` / `home_button_over` | 264x88 | The HOME Menu's buttons |
| `chip_off` / `chip_on` | 212x36 | An option's value box, at its default / changed |
| `row_focus` | 548x44 | The highlighted row in a list |
| `step_back` / `step_back_over`, `step_forward` / `step_forward_over` | 44x44 | The arrows either side of an option |
| `switch_on` / `switch_off` | 68x40 | On/Off switches |
| `panel_game` | 580x240 | The white panel behind a game page's list |
| `panel_settings` | 580x284 | The white panel behind Settings |
| `popup` | 580x284 | A popup's box: the title in a band across its top 44 pixels, a narrower band across the bottom 18 |
| `bar` | 640x124 | Home's bottom bar (its top edge is a curve; see `bar` under [shape]) |
| `banner_stripes` | 640x192 | Drawn over a game page's banner while it shows the game's colour; once the game's box art is on the card, the banner is that art, blurred |
| `arrow_left` / `arrow_left_over`, `arrow_right` / `arrow_right_over` | 48x48 | Home's page arrows |
| `scroll_up` / `scroll_up_over`, `scroll_down` / `scroll_down_over` | 44x44 | A list's scroll arrows |
| `icon_drives` | 28x28 | Drive icon |
| `icon_gear` | 28x28 | Gear icon |
| `icon_search` | 28x28 | Magnifier icon on the search button |
| `icon_star`, `icon_star_on` | 28x28 | Favourite button on a game's banner: not a favourite, a favourite |
| `tile9`, `tile9_over`, `tile9_primary`, `tile9_primary_over` | 64x64 | The game page's and Settings' tiles, stretched with 24-pixel corners kept: plain, lit, the main one (Play), the main one lit |
| `prompt_button` | 24x24 | The round badge under a button's name in the prompts along the bottom of a screen, for a button without a picture of its own (A, B, +, -, 1, 2, HOME, C and Z have Kenney's, drawn in `glyph`) |
| `icon_play`, `icon_mods`, `icon_cheats`, `icon_saves`, `icon_info`, `icon_help` | 28x28 | The game page's tiles: Play, Mods, Cheats, Saves, About; Help in Settings |
| `icon_picture`, `icon_controllers`, `icon_online`, `icon_home`, `icon_sound`, `icon_power` | 28x28 | Settings' tiles: Picture, Controllers, Online, Home screen, Look and sound, Leave RiftWii |
| `icon_disc` | 40x40 | Disc icon (name and channel tiles) |
| `disc_case` | 128x176 | The Disc drive's box: its cover tile, and its front and back on the shelf |
| `pointer1` to `pointer4` | 96x96 | Each player's pointer; the fingertip must be at the centre (48, 48) |
| `shelf` | 256x64 | Home's shelf: rows 0-47 are its top (the far edge first), rows 48-63 its front edge. It repeats along the shelf, so make the left and right edges meet |
| `background_wide` | 856x480 | `background` for a widescreen menu (optional) |
| `bar_wide` | 856x124 | `bar` for a widescreen menu, its dip (or bump) in the middle (optional) |
| `background_shelf` / `background_shelf_wide` | 640x480 / 856x480 | `background` / `background_wide` while Home shows the shelf, the carousel or 3D covers (Settings > Home tiles), for a background drawn to go with Home's rows of covers (optional) |
| `background_plain` / `background_plain_wide` | 640x480 / 856x480 | `background` / `background_wide` on every screen but Home, for a background drawn around Home (Bookshelf: its wall without the shelves) (optional) |
| `background_channels` / `background_channels_wide` | 640x480 / 856x480 | `background` / `background_wide` while Home shows Channels, for a background drawn to go with its three rows (Bookshelf: a shelf under each) (optional) |

### Widescreen and screen size

With Settings > **Widescreen menu** on 16:9 the menu is 856 wide instead of
640, its middle 640 where a 4:3 menu's is. A smaller **Screen size**
leaves room around the menu, too. Pictures are never stretched to fill
it:

- `background_wide` and `bar_wide` are used on a widescreen menu when the
  theme has them. Without them, `background` and `bar` are drawn in the
  middle at their own size.
- Past a picture's edges, its edges are mirrored outward, back and forth:
  the whole background, the bar's first and last 176 columns at its
  sides and its lowest 40 rows below it. Keep those parts plain (no
  words or shapes) so the mirror images look like more of the same.
- Rows stay where they are: Home's covers are at the same height on any
  screen, so a background drawn to go with them (shelves under the
  covers, as in Bookshelf) still lines up.

## Music

A `music.ogg` in the theme's folder plays, looping, in the menu instead of
RiftWii's tune (and instead of `sd:/riftwii/music.ogg`). One song per theme:
there are no playlists, but each theme can bring its own.

- **Ogg Vorbis only.** MP3, WAV, FLAC, M4A and Opus don't play, even renamed
  to `.ogg`. Audacity converts them: File > Export Audio, "Ogg Vorbis",
  quality 3 to 5.
- **6 MB at most.** The whole file is loaded into memory when the menu
  starts; a bigger one is skipped (`session.log` says so) and RiftWii's tune
  plays. At quality 4 (about 128 kbit/s) that is about 6 minutes of music.
- Mono or stereo, 44.1 or 48 kHz. It starts over from the beginning when it
  ends.

Settings > **Menu music** still turns music off.

## Sound effects

Optional WAV files in the theme's folder replace RiftWii's own sounds:
`hover.wav` (the tick when the selection moves), `click.wav` (pressing
something), `home_open.wav` and `home_close.wav` (the HOME Menu). Each is a
16-bit PCM WAV (or AIFF), mono or stereo, 4 000 to 48 000 Hz, at most 1 MB
and 5 seconds; a missing or unreadable one keeps RiftWii's (`session.log`
says why). Keep `hover.wav` short: it plays every time the selection moves.
The kit's `sounds/` folder has RiftWii's own four as WAVs. Settings >
**Menu sounds** sets how loud they are.

## Layout and fonts

Layout (where things are on screen) and fonts are not themeable yet.

## The theme kit

The kit is the [themes/kit](../themes/kit) folder: its
[README](../themes/kit/README.txt) walks through a theme step by step, and it
holds a starter `MyTheme/theme.ini` with every setting at its default, guide
pictures that name every colour and picture on screen, and every picture as
a template, exactly as RiftWii draws it (in the default look, Midnight's and
Bookshelf's colours), at the right size. Take only the templates you want to
change: edit one, keep its size, and save it as PNG in your theme's folder.
The samples are [themes/Midnight](../themes/Midnight) and
[themes/Bookshelf](../themes/Bookshelf).

## Make your own

To make your own theme:

1. Copy `themes/Midnight/` to a new folder in `sd:/riftwii/themes/`, for
   example `sd:/riftwii/themes/MyTheme/`.
2. Open `theme.ini` in a text editor.
3. Change `name` under `[theme]` to your theme's name, and `author` to your
   name.
4. Change a few colours under `[colors]`. Check contrast: text must be
   readable on cards and on the backdrop.
5. In RiftWii, open Settings > **Theme**, step to your theme, and accept
   the restart.

Picture sizes are exact. If you draw your own PNG pictures, an image
editor's "canvas size" is the tool for adding margin around your artwork so
it matches the required dimensions.
