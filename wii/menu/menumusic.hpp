// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "riftwii/musiclist.hpp"

// The menu's background music: the player's own pick when there is one
// (Settings > Music file, music_file in settings.txt: an .ogg or .mp3 song
// on the SD card or the USB drive, an .m3u playlist, or a folder whose
// songs play shuffled; riftwii/musiclist.hpp), else the console's System
// Menu sample bank when selected, otherwise a theme's Ogg Vorbis file or
// an Ogg on the SD card, sd:/riftwii/music.ogg (the player's own) or else
// sd:/apps/riftwii/music.ogg (the one the release ships), looped while
// the menu is open and on through a launch's log, until the launch has to
// give the hardware up. menu_music = off in settings.txt (the Settings row)
// keeps it quiet.
namespace riftwii::wii {

// Starts it when the setting is on and a file is there. The file is read
// once, into MEM2, and kept for the menu's life.
void MenuMusicStart();
// Called by the GUI while the menu thread is idle; switches a failed NAND
// stream to the normal Ogg source. Start/stop hold the GUI halted.
void MenuMusicUpdate();
// Called by the menu's thread each time it halts the GUI (the GUI parked,
// so nothing else reads the drives): moves a list on to its next song,
// starts a lone MP3 again, and starts a pick on the USB drive once Home
// has read the drive. Does nothing under a launch.
void MenuMusicService();
// Stops it; before audio is shut down and before any launch.
void MenuMusicStop();
// Before the music is started again for another SD card: forgets the pick's
// songs and whether it failed, so they are read again.
void MenuMusicForgetPick();
// After the drives are read: forgets a pick that had failed (RiftWii's own
// music playing instead), so it can be tried again, and says so; a pick
// that plays is left alone.
bool MenuMusicRetryFailedPick();
// Banner audio pauses either the Ogg player or the System Menu sample renderer.
void MenuMusicPause(bool pause);
// Fades the music out over half a second, then stops it. The launch does
// this when the player chose "Fade out" for Launch music: the
// music is gone before the launch screen and stays off.
void MenuMusicFadeOut();
// Whether a selected music source was found (the Settings row's note says so).
bool MenuMusicFound();
// What became of the player's pick: "" when it plays (or there is none),
// "waiting" while the USB drive it is on is being read, "failed" when
// nothing in it could be played and RiftWii's own music plays instead.
std::string MenuMusicPickProblem();
// One folder for the picker ("sd:/", "usb:/Music"): its folders, then its
// songs and playlists, sorted, hidden names left out. The USB drive only
// once Home has read it.
std::vector<MusicFolderEntry> MenuMusicBrowse(const std::string& folder);
// How many songs a folder pick would play ("sd:/Music/"), its folders'
// included, as MenuMusicStart counts them.
std::size_t MenuMusicFolderSongs(const std::string& folder);

// Keeps the music playing past the menu, under the launch screen and its
// log, instead of stopping it with the menu. Returns false when nothing
// is playing or launch_music = off in settings.txt; the caller then shuts the audio down itself, as before.
bool MenuMusicKeepForLaunch();
// Fades the kept music out over half a second, stops its decoder and shuts
// the audio down, saying `why` in the log. Called before the game's IOS
// is loaded, right before RiftWii shuts down for the game (or a Homebrew
// Channel app run in its place), and when a launch fails. Does nothing
// when no music was kept.
void LaunchMusicEnd(const char* why);
// Whether music kept for the launch is playing now.
bool LaunchMusicPlaying();
// Before a cIOS reload in the launch: fades the kept music out and shuts
// the audio down as LaunchMusicEnd does, remembering where the track was.
void LaunchMusicPauseForReload(const char* why);
// After that reload, once the heap is checked whole: the track again from
// where it was, faded in, under the rest of the launch's log.
void LaunchMusicResumeAfterReload();

}  // namespace riftwii::wii
