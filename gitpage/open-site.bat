REM SPDX-FileCopyrightText: 2026 RiftWii contributors
REM SPDX-License-Identifier: GPL-3.0-or-later
@echo off
cd /d "%~dp0"

echo Starting RiftWii local server with caching disabled...
start "" http://localhost:8000
python serve.py
