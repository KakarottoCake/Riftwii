# Screenshot the Dolphin window region from the screen to a PNG.
param([string]$Out = "D:\AI Projects\Riftwii\build-dolphin\shot.png")
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class WS {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int n);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    public struct RECT { public int L, T, R, B; }
}
"@
Add-Type -AssemblyName System.Drawing
$d = Get-Process Dolphin -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $d) { throw "no Dolphin process" }
$h = $d.MainWindowHandle
Write-Output "minimized=$([WS]::IsIconic($h))"
[void][WS]::ShowWindow($h, 9)
[void][WS]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 800
$r = New-Object WS+RECT
[void][WS]::GetWindowRect($h, [ref]$r)
$w = $r.R - $r.L; $hh = $r.B - $r.T
Write-Output "window ${w}x${hh} at $($r.L),$($r.T)"
$bmp = New-Object System.Drawing.Bitmap($w, $hh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "saved $Out"
