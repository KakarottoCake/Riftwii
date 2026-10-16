@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo Scanning tutorials folder...

(
    echo [
) > tutorials.json

set first=1

for %%F in ("tutorials\*.html") do (
    if exist "%%F" (
        set "name=%%~nxF"

        if !first! EQU 1 (
            >> tutorials.json echo   "!name!"
            set first=0
        ) else (
            >> tutorials.json echo   ,"!name!"
        )
    )
)

>> tutorials.json echo ]

echo Tutorial list created.

echo Starting local server...
start "" http://localhost:8000

python -m http.server 8000
