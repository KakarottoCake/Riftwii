# Adaptive Kirby save-creation run.
# Phase A (no input): wait until the Gecko log shows the boot probes
# (>= 30 C:ffffff96 async deletes) meaning the game sits at its first
# screen. Phase B: hold `2` until save imports land (>= 8 C:00000000
# completions past the usage check), release, linger, kill.
# Fails loudly on leaks, missing probe phase, or no imports.
param([int]$MaxSecs = 600)
$ErrorActionPreference = "Stop"
$root = "D:\AI Projects\Riftwii"
$dolphin = "C:\Users\bryan.MRREJECT\Downloads\dolphin-master-5.0-18995-x64\Dolphin-x64\Dolphin.exe"
$userDir = "$root\build-dolphin\user"
$dol = "$root\riftwii.dol"
$geckoLog = "$root\build-dolphin\gecko.log"
$leaked = Get-Process Dolphin -ErrorAction SilentlyContinue
if ($leaked) { throw "refusing: Dolphin already running (PIDs $($leaked.Id -join ','))" }
Remove-Item $geckoLog -ErrorAction SilentlyContinue
Remove-Item "$userDir\Logs\dolphin.log" -ErrorAction SilentlyContinue
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Kb {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@
$KEYEVENTF_KEYUP = 0x0002
$gecko = Start-Process -FilePath "python" -ArgumentList "`"/d/AI Projects/Riftwii/tools/dolphin/gecko_log.py`" `"/d/AI Projects/Riftwii/build-dolphin/gecko.log`" 700" -PassThru -WindowStyle Hidden
$d = Start-Process -FilePath $dolphin -ArgumentList "-u `"$userDir`" -e `"$dol`" -b" -PassThru
if ((Get-Process Dolphin -ErrorAction SilentlyContinue).Count -ne 1) { throw "expected 1 Dolphin" }
function GeckoStats {
    $lines = Get-Content $geckoLog -ErrorAction SilentlyContinue
    $probes = ($lines | Where-Object { $_ -eq "C:ffffff96" }).Count
    $oks = ($lines | Where-Object { $_ -eq "C:00000000" }).Count
    return @{ probes = $probes; oks = $oks; total = $lines.Count }
}
$start = Get-Date
$held = $false
try { [void][Kb]::SetForegroundWindow($d.MainWindowHandle) } catch {}
while (((Get-Date) - $start).TotalSeconds -lt $MaxSecs) {
    Start-Sleep -Seconds 10
    if (-not (Get-Process -Id $d.Id -ErrorAction SilentlyContinue)) { throw "Dolphin died early" }
    $s = GeckoStats
    Write-Output ("t={0:N0}s lines={1} probes={2} oks={3} held={4}" -f ((Get-Date) - $start).TotalSeconds, $s.total, $s.probes, $s.oks, $held)
    if (-not $held -and $s.probes -ge 30) {
        Write-Output "probe burst seen, holding 2..."
        try { [void][Kb]::SetForegroundWindow($d.MainWindowHandle) } catch {}
        [Kb]::keybd_event(0x32, 0, 0, [UIntPtr]::Zero)
        $held = $true
    }
    if ($held -and $s.oks -ge 8) {
        Write-Output "imports landing, releasing 2 and lingering 45s..."
        [Kb]::keybd_event(0x32, 0, $KEYEVENTF_KEYUP, [UIntPtr]::Zero)
        $held = $false
        Start-Sleep -Seconds 45
        break
    }
}
if ($held) { [Kb]::keybd_event(0x32, 0, $KEYEVENTF_KEYUP, [UIntPtr]::Zero) }
taskkill /IM Dolphin.exe /F | Out-Null
Start-Sleep -Seconds 5
if (Get-Process Dolphin -ErrorAction SilentlyContinue) { throw "kill failed" }
Stop-Process -Id $gecko.Id -Force -ErrorAction SilentlyContinue
$s = GeckoStats
Write-Output ("FINAL lines={0} probes={1} oks={2}" -f $s.total, $s.probes, $s.oks)
Get-Content $geckoLog -ErrorAction SilentlyContinue | Select-Object -Last 15
