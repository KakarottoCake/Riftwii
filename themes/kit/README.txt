RiftWii theme kit
=================

Everything you need to make a menu theme for RiftWii: its colours, its
pictures and its music.

What's in here
--------------

  MyTheme/theme.ini      A starter theme. Every setting is listed with
                         RiftWii's default value and what it changes.
  guide/                 Pictures of the menu with every picture and
                         colour you can change marked by its name: start
                         here to find what you want to change.
  templates/Default/     Every picture a theme can replace, exactly as
                         RiftWii draws them in its default look.
  templates/Midnight/    The same pictures in Midnight's colours.
  templates/Bookshelf/   The same pictures in Bookshelf's colours.
  sounds/                RiftWii's own sound effects as WAV files, under
                         the names a theme uses for them.
  Midnight/, Bookshelf/  The two sample themes (in the kit's zip; on
                         GitHub they are in the themes folder, next to
                         this one). Midnight is colours only; Bookshelf
                         has its own pictures and the iOS 6 HOME Menu.
  THEMES.md              The full reference (docs/THEMES.md on GitHub):
                         every setting, every picture, its size and
                         where it shows.

A theme is one folder on your SD card, sd:/riftwii/themes/<name>/, with a
theme.ini and, if you like, PNG pictures, a music.ogg and sound effects. You only need the
parts you change: RiftWii draws everything else in your theme's colours.

Make a theme in 5 minutes (colours only)
----------------------------------------

1. Copy the MyTheme folder to sd:/riftwii/themes/ on your SD card, so you
   have sd:/riftwii/themes/MyTheme/theme.ini. Rename the folder if you
   like.
2. Open theme.ini in a text editor. Set name and author under [theme].
3. Change colours under [colors]. They are #RRGGBB, or #RRGGBBAA where AA
   is the opacity (00 see-through, FF solid). The guide pictures show
   where each one goes (blue labels). Keep text readable on cards and on
   the backdrop.
4. In RiftWii, open Settings > Theme, pick your theme and let the menu
   restart.

A good start: accent (every highlight, the bar's line), card (tiles,
buttons, panels), bar (Home's bottom bar and the title bands), backdrop and
ink (text). Buttons, panels, the clock and the HOME Menu all follow.

Anything RiftWii can't read is skipped and keeps its default, so a typo
never breaks the menu. Skipped lines are listed in sd:/riftwii/session.log
(lines starting "Theme:").

Comments go on their own line, starting with # or ;. A comment after a
value on the same line is read as part of the value.

Shape settings
--------------

Under [shape] in theme.ini:

  corners    How round corners are, 0 (square) to 2.
  gloss      yes: a shine across the top of tiles and buttons.
  bar        dip: Home's bar sinks under the clock, as the Wii Menu's
             (the default). bump: it rises there instead.
  home_menu  wii: the HOME Menu as the Wii's own (the default).
             ios6: glossy bars in your bar colour, dark linen between
             them, iOS 6 style buttons with a red Power off (Bookshelf).

Replace pictures
----------------

1. Find the picture in the guide pictures (pink labels), then take it from
   templates/Default (or templates/Midnight, templates/Bookshelf), for
   example tile.png, and edit it in any image editor that keeps
   transparency (GIMP, Krita, Paint.NET, Photopea).
2. Keep the exact same size in pixels: a picture of any other size is
   skipped. The size includes a see-through margin where the shadow and
   glow go, so keep your artwork where the template's is.
3. Save it as PNG, 2 MB at most, with the same name, into your theme's
   folder next to theme.ini (sd:/riftwii/themes/MyTheme/tile.png).

Pictures ending in _over are the highlighted versions (where the pointer
or the D-pad is). The pointers (pointer1.png to pointer4.png) aim with the
exact centre of the picture, pixel (48, 48): keep the fingertip there.

Backgrounds
-----------

background.png (640x480) replaces the whole backdrop, stripes included.
All of these are optional; each falls back to background.png:

  background_wide.png        856x480, for a widescreen (16:9) menu.
  background_channels.png    Home in the Channels view (Bookshelf puts a
                             shelf under each row of channels).
  background_shelf.png       Home in the Shelf view.
  background_plain.png       Every screen but Home (Settings, a game's
                             page): Bookshelf's wall without the shelves.

Each has a _wide version too (856x480). bar.png (640x124) and
bar_wide.png (856x124) are Home's bottom bar; keep the shape your [shape]
bar setting says (dip or bump) so the clock and buttons sit right.

Music
-----

Put a music.ogg in your theme's folder and it plays, looping, in the menu
instead of RiftWii's tune. It is one song per theme (no playlists); each
theme can bring its own, so switching themes switches the music.

  Format   Ogg Vorbis (.ogg) only. MP3, WAV, FLAC, M4A and Opus files
           don't play, even renamed to .ogg: convert them first. In
           Audacity: File > Export Audio, format "Ogg Vorbis", quality
           3 to 5 is plenty for a TV.
  Size     6 MB at most. The whole file is loaded into the Wii's memory
           when the menu starts; a bigger one is skipped and RiftWii's
           tune plays instead (session.log says "Menu music: ... at most
           ... are played").
  Length   Anything that fits in 6 MB: at quality 4 (about 128 kbit/s)
           that is about 6 minutes. Lower the quality for a longer song.
  Sound    Mono or stereo, 44.1 or 48 kHz.
  Loop     The song starts over from the beginning when it ends, so a
           song whose end leads into its start loops best.

Settings > Menu music still turns music off. Without a music.ogg in the
theme, RiftWii plays sd:/riftwii/music.ogg if there is one (the same
rules), else its own tune.

Sound effects
-------------

Put any of these WAV files in your theme's folder, next to theme.ini, and
they play instead of RiftWii's own:

  hover.wav       The tick when the pointer or the D-pad moves onto
                  something.
  click.wav       Pressing a button, a tile or a row.
  home_open.wav   The HOME Menu opening.
  home_close.wav  The HOME Menu closing.

  Format   WAV (or AIFF), 16-bit PCM, mono or stereo, any rate from
           4 000 to 48 000 Hz (44.1 or 48 kHz is best). In Audacity:
           File > Export Audio, "WAV (Microsoft)", "Signed 16-bit PCM".
  Size     1 MB and 5 seconds at most each; a longer sound is cut at
           5 seconds. Keep hover.wav very short (a fraction of a second):
           it plays every time the selection moves.

The sounds/ folder has RiftWii's own four as a starting point. Each one is
optional: leave one out and RiftWii's plays. Settings > Menu sounds sets
how loud they are (Quiet softens hover.wav). A sound RiftWii can't read is
skipped, and session.log says why (lines starting "Theme:").

Sharing your theme
------------------

Zip your theme's folder and share it. Others unzip it into
sd:/riftwii/themes/ and pick it under Settings > Theme. Show it off on the
RiftWii Discord: https://discord.gg/gGsTdjjQaK

The templates, the guide pictures and the starter theme.ini in this kit
are yours to use, change and share in your own themes, with or without
credit.
