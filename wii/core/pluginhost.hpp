// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// The network and file entries of the plugin table (riftwii/plugin_api.h,
// "Added after progress"), for a plugin with a screen of its own: the app
// store (docs/APPSTORE.md). plugins.cpp puts them in kHost; their screen
// partners are in wii/menu/pluginscreen.hpp.
namespace riftwii::wii::pluginhost {

int32_t NetReady(char* error, uint32_t error_bytes);
int32_t HttpGet(const char* url, uint8_t* buffer, uint32_t capacity, uint32_t* length, char* error,
                uint32_t error_bytes);
int32_t HttpDownload(const char* url, const char* path, uint32_t max_bytes, uint8_t sha256[32],
                     void (*progress)(uint32_t done, void* arg), void* arg, char* error, uint32_t error_bytes);
int32_t BackgroundStart(void (*job)(void* arg), void* arg);
uint32_t BackgroundRunning();
void BackgroundCancel();
int32_t RemovePath(const char* path);
int32_t RenamePath(const char* from, const char* to);
int32_t ListDirectory(const char* path, void (*each)(const char* name, uint32_t is_directory, void* arg), void* arg);
int32_t SdFreeKib(uint32_t* kib);

}  // namespace riftwii::wii::pluginhost
