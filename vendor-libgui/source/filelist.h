/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * imagelist.h
 * Contains a list of all of the files in the images, fonts, sounds folders
 ***************************************************************************/

#ifndef _FILELIST_H_
#define _FILELIST_H_

#include <gccore.h>

// Fonts
#include "font_ttf.h"

// Languages
#include "en_lang.h"

// Sounds
#include "bg_music_ogg.h"
#include "button_over_pcm.h"
#include "button_click_ogg.h"
#include "home_open_ogg.h"
#include "home_close_ogg.h"

// RiftWii: the press and the two HOME Menu sounds are stored as Ogg Vorbis
// (the *_ogg arrays above) and decoded once at start into MEM2 by
// wii/menu/menusounds.cpp, which defines these: 48 kHz 16-bit stereo, what
// GuiSound hands ASND. Null and 0 until decoded, or when the decode failed.
extern const u8* button_click_pcm;
extern s32 button_click_pcm_size;
extern const u8* home_open_pcm;
extern s32 home_open_pcm_size;
extern const u8* home_close_pcm;
extern s32 home_close_pcm_size;

// Images
#include "button_png.h"
#include "button_over_png.h"
#include "button_large_png.h"
#include "button_large_over_png.h"
#include "dialogue_box_png.h"
#include "bg_options_png.h"
#include "bg_options_entry_png.h"
#include "folder_png.h"
#include "bg_file_selection_png.h"
#include "bg_file_selection_entry_png.h"
#include "scrollbar_png.h"
#include "scrollbar_arrowup_png.h"
#include "scrollbar_arrowup_over_png.h"
#include "scrollbar_arrowdown_png.h"
#include "scrollbar_arrowdown_over_png.h"
#include "scrollbar_box_png.h"
#include "scrollbar_box_over_png.h"
#include "keyboard_textbox_png.h"
#include "keyboard_key_png.h"
#include "keyboard_key_over_png.h"
#include "keyboard_mediumkey_png.h"
#include "keyboard_mediumkey_over_png.h"
#include "keyboard_largekey_png.h"
#include "keyboard_largekey_over_png.h"
#include "player1_point_png.h"
#include "player2_point_png.h"
#include "player3_point_png.h"
#include "player4_point_png.h"
#include "player1_grab_png.h"
#include "player2_grab_png.h"
#include "player3_grab_png.h"
#include "player4_grab_png.h"

#endif
