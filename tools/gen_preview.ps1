# WinFence 效果示意图生成（GDI+ 程序化绘制，还原真实配色：深蓝黑玻璃 + 霓虹青 accent）
Add-Type -AssemblyName System.Drawing

$W = 1600; $H = 1000
$bmp = New-Object System.Drawing.Bitmap($W, $H)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality

$accent = [System.Drawing.Color]::FromArgb(255, 62, 201, 245)   # #3EC9F5 霓虹青

function RoundPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function SoftShadow($g, [float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    # 6 层同心外扩圆角矩形，alpha 递减 → 柔和投影
    $steps = @( @(0, 100), @(7, 62), @(15, 36), @(24, 19), @(34, 8), @(44, 3) )
    foreach ($s in $steps) {
        $grow = $s[0]; $a = $s[1]
        $b = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb($a, 0, 0, 0))
        $p = RoundPath ($x - $grow) ($y - $grow + 12) ($w + 2 * $grow) ($h + 2 * $grow) ($r + $grow)
        $g.FillPath($b, $p)
        $b.Dispose(); $p.Dispose()
    }
}

function GlassPanel($g, [float]$x, [float]$y, [float]$w, [float]$h, [float]$r, [int]$topA, [int]$botA) {
    $rect = New-Object System.Drawing.RectangleF($x, $y, $w, $h)
    $lg = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect,
        [System.Drawing.Color]::FromArgb($topA, 44, 52, 74),
        [System.Drawing.Color]::FromArgb($botA, 12, 15, 26), 90)
    $p = RoundPath $x $y $w $h $r
    $g.FillPath($lg, $p)
    $lg.Dispose(); $p.Dispose()
}

function Hairline($g, [float]$x, [float]$y, [float]$w, [float]$h, [float]$r, [int]$a, [float]$width) {
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb($a, 255, 255, 255), $width)
    $p = RoundPath $x $y $w $h $r
    $g.DrawPath($pen, $p)
    $pen.Dispose(); $p.Dispose()
}

# ================= 壁纸背景 =================
$bgRect = New-Object System.Drawing.Rectangle(0, 0, $W, $H)
$bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush($bgRect,
    [System.Drawing.Color]::FromArgb(255, 30, 36, 54),
    [System.Drawing.Color]::FromArgb(255, 9, 11, 20), 90)
$g.FillRectangle($bg, $bgRect)
$bg.Dispose()
# 顶部径向光斑
$gp = New-Object System.Drawing.Drawing2D.GraphicsPath
$gp.AddEllipse(350, -420, 900, 780)
$pgb = New-Object System.Drawing.Drawing2D.PathGradientBrush($gp)
$pgb.CenterColor = [System.Drawing.Color]::FromArgb(70, 96, 130, 210)
$pgb.SurroundColors = @([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$g.FillPath($pgb, $gp)
$pgb.Dispose(); $gp.Dispose()

# ================= 栅栏面板 =================
$fx = 150; $fy = 130; $fw = 620; $fh = 660; $fr = 28
SoftShadow $g $fx $fy $fw $fh $fr
GlassPanel $g $fx $fy $fw $fh $fr 245 250
Hairline $g ($fx + 1) ($fy + 1) ($fw - 2) ($fh - 2) ($fr - 1) 55 1.2   # 顶部内高光
Hairline $g $fx $fy $fw $fh $fr 26 1.5                                    # 发丝外描边

# 标题行
$bar = New-Object System.Drawing.SolidBrush($accent)
$p = RoundPath ($fx + 22) ($fy + 24) 3.5 13 1.75
$g.FillPath($bar, $p); $p.Dispose(); $bar.Dispose()
$fontTitle = New-Object System.Drawing.Font("Microsoft YaHei UI", 21, [System.Drawing.FontStyle]::Bold)
$brushTxt = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(240, 255, 255, 255))
$g.DrawString("工作文档", $fontTitle, $brushTxt, ($fx + 42), ($fy + 16))
$fontCount = New-Object System.Drawing.Font("Consolas", 18, [System.Drawing.FontStyle]::Bold)
$g.DrawString("12", $fontCount, (New-Object System.Drawing.SolidBrush($accent)), ($fx + $fw - 66), ($fy + 22))
$sepPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(45, 62, 201, 245), 1)
$g.DrawLine($sepPen, $fx + 20, $fy + 62, $fx + $fw - 20, $fy + 62)
$sepPen.Dispose()

# 图标网格 5×3
$cols = 5
$tile = 88; $gapX = 36; $gapY = 26
$gridX0 = $fx + 30; $gridY0 = $fy + 92
$tileColors = @(
    @([System.Drawing.Color]::FromArgb(255, 62, 201, 245), [System.Drawing.Color]::FromArgb(255, 20, 110, 160)),
    @([System.Drawing.Color]::FromArgb(255, 167, 139, 250), [System.Drawing.Color]::FromArgb(255, 90, 62, 180)),
    @([System.Drawing.Color]::FromArgb(255, 52, 211, 153), [System.Drawing.Color]::FromArgb(255, 16, 120, 90)),
    @([System.Drawing.Color]::FromArgb(255, 251, 191, 36), [System.Drawing.Color]::FromArgb(255, 180, 110, 14)),
    @([System.Drawing.Color]::FromArgb(255, 251, 113, 133), [System.Drawing.Color]::FromArgb(255, 180, 40, 70)),
    @([System.Drawing.Color]::FromArgb(255, 96, 165, 250), [System.Drawing.Color]::FromArgb(255, 30, 80, 180)),
    @([System.Drawing.Color]::FromArgb(255, 45, 212, 191), [System.Drawing.Color]::FromArgb(255, 13, 120, 110)),
    @([System.Drawing.Color]::FromArgb(255, 251, 146, 60), [System.Drawing.Color]::FromArgb(255, 190, 80, 20)),
    @([System.Drawing.Color]::FromArgb(255, 196, 181, 253), [System.Drawing.Color]::FromArgb(255, 110, 90, 200)),
    @([System.Drawing.Color]::FromArgb(255, 134, 239, 172), [System.Drawing.Color]::FromArgb(255, 40, 150, 90)),
    @([System.Drawing.Color]::FromArgb(255, 125, 211, 252), [System.Drawing.Color]::FromArgb(255, 30, 120, 200)),
    @([System.Drawing.Color]::FromArgb(255, 253, 224, 71), [System.Drawing.Color]::FromArgb(255, 200, 140, 10))
)
$labels = @("季度报告", "产品设计", "素材库", "浏览器", "音乐", "项目计划", "会议纪要", "财务报表", "参考资料", "壁纸收藏", "代码仓库", "下载文件")
$fontLabel = New-Object System.Drawing.Font("Microsoft YaHei UI", 13, [System.Drawing.FontStyle]::Regular)
$brushLabel = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(215, 255, 255, 255))
$fmt = New-Object System.Drawing.StringFormat
$fmt.Alignment = [System.Drawing.StringAlignment]::Center
for ($i = 0; $i -lt 12; $i++) {
    $col = $i % $cols; $row = [math]::Floor($i / $cols)
    $tx = $gridX0 + $col * ($tile + $gapX)
    $ty = $gridY0 + $row * ($tile + $gapY + 34)
    $c0 = $tileColors[$i][0]; $c1 = $tileColors[$i][1]
    $tr = New-Object System.Drawing.RectangleF($tx, $ty, $tile, $tile)
    $lg = New-Object System.Drawing.Drawing2D.LinearGradientBrush($tr, $c0, $c1, 90)
    $p = RoundPath $tx $ty $tile $tile 20
    $g.FillPath($lg, $p)
    $lg.Dispose(); $p.Dispose()
    # 图标顶部玻璃反光
    $hl = RoundPath ($tx + 4) ($ty + 4) ($tile - 8) (($tile - 8) / 2) 15
    $hb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(45, 255, 255, 255))
    $g.FillPath($hb, $hl)
    $hb.Dispose(); $hl.Dispose()
    $lp = [System.Drawing.RectangleF]::new([single]($tx - 14), [single]($ty + $tile + 6), [single]($tile + 28), [single]24)
    $g.DrawString($labels[$i], $fontLabel, $brushLabel, $lp, $fmt)
}

# ================= Dock 栏 =================
$dw = 920; $dh = 128; $dx = ($W - $dw) / 2; $dy = $H - $dh - 40; $dr = 40
SoftShadow $g $dx $dy $dw $dh $dr
GlassPanel $g $dx $dy $dw $dh $dr 210 220
Hairline $g ($dx + 1) ($dy + 1) ($dw - 2) ($dh - 2) ($dr - 1) 48 1.2
Hairline $g $dx $dy $dw $dh $dr 24 1.5

$dcount = 8
$dtile = 76; $dgap = 26
$dtotal = $dcount * $dtile + ($dcount - 1) * $dgap
$dx0 = $dx + ($dw - $dtotal) / 2
$dty = $dy + 22
$dcolors = $tileColors[0..7]
for ($i = 0; $i -lt $dcount; $i++) {
    $tx = $dx0 + $i * ($dtile + $dgap)
    $c0 = $dcolors[$i][0]; $c1 = $dcolors[$i][1]
    $tr = New-Object System.Drawing.RectangleF($tx, $dty, $dtile, $dtile)
    $lg = New-Object System.Drawing.Drawing2D.LinearGradientBrush($tr, $c0, $c1, 90)
    $p = RoundPath $tx $dty $dtile $dtile 18
    $g.FillPath($lg, $p)
    $lg.Dispose(); $p.Dispose()
    $hl = RoundPath ($tx + 3) ($dty + 3) ($dtile - 6) (($dtile - 6) / 2) 14
    $hb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(45, 255, 255, 255))
    $g.FillPath($hb, $hl)
    $hb.Dispose(); $hl.Dispose()
    # 倒影
    $rb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(34, $c0.R, $c0.G, $c0.B))
    $rp = RoundPath $tx ($dty + $dtile + 4) $dtile 17 14
    $g.FillPath($rb, $rp)
    $rb.Dispose(); $rp.Dispose()
}

# 保存
$out = "E:\zy\WinFence\docs\preview.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host "saved: $out"
