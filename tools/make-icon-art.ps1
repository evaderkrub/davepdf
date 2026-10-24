# Draws the davepdf application mark as a 1024x1024 PNG: a red rounded tile
# (FreeWili accent #c83030) carrying a white page with a folded corner and
# three text lines. make-icon.ps1 then packs it into the multi-size .ico.
param([string]$Destination = "$PSScriptRoot/../assets/icons/davepdf.png")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$size = 1024
$bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    # Tile.
    $pad = 52; $radius = 200
    $tile = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $r = [System.Drawing.Rectangle]::new($pad, $pad, $size - 2*$pad, $size - 2*$pad)
    $d = $radius * 2
    $tile.AddArc($r.X, $r.Y, $d, $d, 180, 90)
    $tile.AddArc($r.Right - $d, $r.Y, $d, $d, 270, 90)
    $tile.AddArc($r.Right - $d, $r.Bottom - $d, $d, $d, 0, 90)
    $tile.AddArc($r.X, $r.Bottom - $d, $d, $d, 90, 90)
    $tile.CloseFigure()
    $brush = [System.Drawing.Drawing2D.LinearGradientBrush]::new($r, [System.Drawing.Color]::FromArgb(255, 214, 58, 58), [System.Drawing.Color]::FromArgb(255, 178, 36, 36), 90)
    $g.FillPath($brush, $tile)
    # Page with folded corner.
    $ivory = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 250, 247, 242))
    $fold = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 226, 214, 204))
    $left = 300; $top = 220; $right = 724; $bottom = 804; $corner = 130
    $page = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $page.AddPolygon([System.Drawing.Point[]]@(
        [System.Drawing.Point]::new($left, $top),
        [System.Drawing.Point]::new($right - $corner, $top),
        [System.Drawing.Point]::new($right, $top + $corner),
        [System.Drawing.Point]::new($right, $bottom),
        [System.Drawing.Point]::new($left, $bottom)))
    $g.FillPath($ivory, $page)
    $g.FillPolygon($fold, [System.Drawing.Point[]]@(
        [System.Drawing.Point]::new($right - $corner, $top),
        [System.Drawing.Point]::new($right - $corner, $top + $corner),
        [System.Drawing.Point]::new($right, $top + $corner)))
    # Text lines in the tile red.
    $red = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 200, 48, 48))
    $g.FillRectangle($red, 356, 440, 312, 40)
    $g.FillRectangle($red, 356, 530, 312, 40)
    $g.FillRectangle($red, 356, 620, 210, 40)
    $bitmap.Save([IO.Path]::GetFullPath($Destination), [System.Drawing.Imaging.ImageFormat]::Png)
} finally { $g.Dispose(); $bitmap.Dispose() }
Write-Output "Created $Destination"
