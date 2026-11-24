// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "libwiigui/gui.h"
#include "riftwii/plugin_api.h"

// A plugin's own screen (riftwii/plugin_api.h's screen group): the app
// store's pages, drawn by the menu in its theme from the commands the
// plugin hands over each frame. The plugin runs on the menu's thread; the
// GUI thread draws the last frame's commands and gathers the controllers'
// input for the next. Everything here is the table's entries, and the few
// helpers rift_menu.cpp lends (its popups and keyboard are its own).
namespace riftwii::wii::pluginscreen {

struct Hooks {
    void (*halt)() = nullptr;
    void (*resume)() = nullptr;
    // The menu's window, the screen's parent.
    GuiWindow* (*window)() = nullptr;
    // rift_menu.cpp's ShowPopup: 0 for the first button, 1 for the second. With the GUI halted.
    int (*popup)(const std::string& title, const std::string& body, const std::string& yes,
                 const std::string& no) = nullptr;
    // rift_menu.cpp's ShowPopupSafeFirst: 0 for the safe (first) button, 1 for the risky one. With the GUI halted.
    int (*popup_safe_first)(const std::string& title, const std::string& body, const std::string& safe,
                            const std::string& risky) = nullptr;
    // rift_menu.cpp's AskSearch. With the GUI halted.
    bool (*search)(std::string& text,
                   const std::function<std::string(const std::string&, std::string& matches, std::string& next)>&
                       typed) = nullptr;
};
// Called once by the menu at start.
void SetHooks(const Hooks& hooks);

int32_t Open(rwp_screen_info* out);
int32_t Frame(const rwp_draw* commands, uint32_t count, rwp_input* input);
void Close();
uint32_t TextureCreate(const uint8_t* rgba, uint32_t width, uint32_t height);
void TextureFree(uint32_t handle);
int32_t TextWidth(const char* text, uint32_t size);
const char* Translate(const char* english);
void Sound(uint32_t which);
int32_t AskSearch(char* text, uint32_t bytes,
                  void (*typed)(const char* text, char* line, uint32_t line_bytes, char* matches,
                                uint32_t matches_bytes, char* next, uint32_t next_bytes, void* arg),
                  void* arg);
int32_t AskChoice(const char* title, const char* text, const char* yes, const char* no);
int32_t AskRisky(const char* title, const char* text, const char* safe, const char* risky);

}  // namespace riftwii::wii::pluginscreen
