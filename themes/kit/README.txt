RiftWii theme kit
=================

Everything you need to make a menu theme for RiftWii {version}.

What's in here
--------------

  MyTheme/theme.ini      A starter theme. Every setting is listed with
                         RiftWii's default value and what it changes.
  Midnight/theme.ini     The dark sample theme, to look at or start from.
  templates/Default/     All 43 pictures a theme can replace, exactly as
                         RiftWii draws them in its default look.
  templates/Midnight/    The same pictures in Midnight's colours.
  THEMES.md              The full reference: every setting, every picture,
                         its size and where it shows.

Make a theme in 5 minutes (colours only)
----------------------------------------

1. Copy the MyTheme folder to sd:/riftwii/themes/ on your SD card, so you
   have sd:/riftwii/themes/MyTheme/theme.ini. Rename the folder if you
   like.
2. Open theme.ini in a text editor. Set name and author under [theme].
3. Change colours under [colors]. They are #RRGGBB, or #RRGGBBAA where AA
   is the opacity (00 see-through, FF solid). Keep text readable on cards
   and on the backdrop.
4. In RiftWii, open Settings > Theme, pick your theme and let the menu
   restart.

Anything RiftWii can't read is skipped and keeps its default, so a typo
never breaks the menu. Skipped lines are listed in sd:/riftwii/session.log
(lines starting "Theme:").

Comments go on their own line, starting with # or ;. A comment after a
value on the same line is read as part of the value.

Replace pictures
----------------

1. Pick a picture from templates/Default (or templates/Midnight), for
   example tile.png, and edit it in any image editor that keeps
   transparency (GIMP, Krita, Paint.NET, Photopea).
2. Keep the exact same size in pixels. A picture of any other size is
   skipped. The size includes a see-through margin where the shadow and
   glow go: keep your artwork in the same place as the template's.
3. Save it as PNG with the same name into your theme's folder, next to
   theme.ini (sd:/riftwii/themes/MyTheme/tile.png).

Pictures ending in _over are the highlighted versions (where the pointer
or the D-pad is). The pointers (pointer1.png to pointer4.png) aim with the
exact centre of the picture, pixel (48, 48): keep the fingertip there.
background.png replaces the whole backdrop, stripes included.

You only need the pictures you change. Anything you leave out is drawn by
RiftWii in your theme's colours.

Music
-----

Put a music.ogg in your theme's folder and it plays in the menu instead
of RiftWii's tune.

Sharing your theme
------------------

Zip your theme's folder and share it. Others unzip it into
sd:/riftwii/themes/ and pick it under Settings > Theme. Show it off on the
RiftWii Discord: https://discord.gg/gGsTdjjQaK

The templates and the starter theme.ini in this kit are yours to use,
change and share in your own themes, with or without credit.
