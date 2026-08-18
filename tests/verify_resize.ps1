# [TEMP] Resize fix verification (delete after use)
$ErrorActionPreference = 'Stop'

Remove-Item "$env:APPDATA\WinFence\config.json", "$env:APPDATA\WinFence\config.json.bak" -Force -ErrorAction SilentlyContinue
Start-Process E:\zy\WinFence\build\Release\WinFence.exe
Start-Sleep -Seconds 5

$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class RS1 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int i);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static IntPtr FindByClassAndPid(uint target, string cls) { IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == target) { var sb = new StringBuilder(256); GetClassNameW(h, sb, 256);
        if (sb.ToString() == cls) { found = h; return false; } } return true; }, IntPtr.Zero);
    return found; }
}
'@
Add-Type -TypeDefinition $src

$p = Get-Process WinFence
$h = [RS1]::FindByClassAndPid([uint32]$p.Id, 'WinFenceFenceWnd')
$style = [RS1]::GetWindowLongW($h, -16)
$thick = ($style -band 0x00040000) -ne 0
Write-Host "WS_THICKFRAME present: $thick (style=0x$($style.ToString('X')))"
$lp = [IntPtr](300 -bor (340 -shl 16))
$ht = [RS1]::SendMessageW($h, 0x0084, [IntPtr]::Zero, $lp).ToInt64()
Write-Host "HT @ bottom-right corner: $ht (17=HTBOTTOMRIGHT expected)"
$lp2 = [IntPtr](100 -bor (40 -shl 16))
$ht2 = [RS1]::SendMessageW($h, 0x0084, [IntPtr]::Zero, $lp2).ToInt64()
Write-Host "HT @ title bar: $ht2 (2=HTCAPTION expected)"
$lp3 = [IntPtr](5 -bor (160 -shl 16))
$ht3 = [RS1]::SendMessageW($h, 0x0084, [IntPtr]::Zero, $lp3).ToInt64()
Write-Host "HT @ left edge: $ht3 (10=HTLEFT expected)"
Stop-Process -Id $p.Id -Force -Confirm:$false
Write-Host "done"
