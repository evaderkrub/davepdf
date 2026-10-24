param([string]$Source = "$PSScriptRoot/../assets/icons/davepdf.png", [string]$Destination = "$PSScriptRoot/../assets/icons/davepdf.ico")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$inputImage = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source))
try {
    $sizes = @(16,20,24,32,40,48,64,128,256)
    $images = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $stream = [System.IO.MemoryStream]::new()
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($inputImage,[System.Drawing.Rectangle]::new(0,0,$size,$size))
            $bitmap.Save($stream,[System.Drawing.Imaging.ImageFormat]::Png)
            $images.Add($stream.ToArray())
        } finally { $graphics.Dispose(); $bitmap.Dispose(); $stream.Dispose() }
    }
    $file = [System.IO.File]::Create([IO.Path]::GetFullPath($Destination))
    $writer = [System.IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i=0; $i -lt $sizes.Count; $i++) {
            $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
            $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
            $offset += $images[$i].Length
        }
        foreach ($bytes in $images) { $writer.Write($bytes) }
    } finally { $writer.Dispose(); $file.Dispose() }
} finally { $inputImage.Dispose() }
Write-Output "Created $Destination (16, 20, 24, 32, 40, 48, 64, 128 and 256 px)."
