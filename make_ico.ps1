param([string]$Out)
Add-Type -AssemblyName System.Drawing

function RoundRect($x, $y, $w, $h, $r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    $p
}

# 深色晶片底 + 白色溫度計，水銀為橘紅色
function Draw([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $u = $s / 16.0

    # 晶片接腳 (16 px 以上才畫)
    if ($s -ge 32) {
        $pin = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 150, 160, 175))
        foreach ($i in 0..2) {
            $c = (4.5 + $i * 3.5) * $u
            $g.FillRectangle($pin, $c, 0.4 * $u, 1.0 * $u, 2 * $u)
            $g.FillRectangle($pin, $c, 13.6 * $u, 1.0 * $u, 2 * $u)
            $g.FillRectangle($pin, 0.4 * $u, $c, 2 * $u, 1.0 * $u)
            $g.FillRectangle($pin, 13.6 * $u, $c, 2 * $u, 1.0 * $u)
        }
    }
    $m = if ($s -ge 32) { 1.8 * $u } else { 0.5 * $u }
    $body = RoundRect $m $m ($s - 2 * $m) ($s - 2 * $m) (2.2 * $u)
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, 0), (New-Object System.Drawing.PointF 0, $s), ([System.Drawing.Color]::FromArgb(255, 52, 60, 76)), ([System.Drawing.Color]::FromArgb(255, 28, 32, 42))
    $g.FillPath($bg, $body)

    # 溫度計：管身 + 球
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $hot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 92, 56))
    $cx = 8 * $u
    $tw = 3.2 * $u; $bulb = 5.4 * $u
    $g.FillPath($white, (RoundRect ($cx - $tw / 2) (2.6 * $u) $tw (7.8 * $u) ($tw / 2)))
    $g.FillEllipse($white, $cx - $bulb / 2, 8.4 * $u, $bulb, $bulb)
    $iw = 1.4 * $u; $ib = 3.4 * $u
    $g.FillRectangle($hot, $cx - $iw / 2, 5.4 * $u, $iw, 5.0 * $u)
    $g.FillEllipse($hot, $cx - $ib / 2, 9.4 * $u, $ib, $ib)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $ms.ToArray()
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$pngs = foreach ($s in $sizes) { , (Draw $s) }

$fs = [System.IO.File]::Create($Out)
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$off = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $pngs[$i].Length
    $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256)); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$len); $w.Write([uint32]$off)
    $off += $len
}
foreach ($p in $pngs) { $w.Write($p) }
$w.Close()

# 預覽圖 (各尺寸排一列)
$prev = New-Object System.Drawing.Bitmap 570, 260
$g = [System.Drawing.Graphics]::FromImage($prev)
$g.Clear([System.Drawing.Color]::FromArgb(255, 240, 240, 240))
$x = 8
foreach ($p in $pngs) {
    $img = [System.Drawing.Image]::FromStream((New-Object System.IO.MemoryStream (, $p)))
    if ($img.Width -eq 256) { $g.DrawImage($img, 305, 0, 256, 256) } else { $g.DrawImage($img, $x, 8); $x += $img.Width + 6 }
}
$g.Dispose()
$prev.Save([System.IO.Path]::ChangeExtension($Out, '.preview.png'))


