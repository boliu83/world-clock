# Convert wall-clock.png into a multi-size ICO embedding PNG payloads.
# Output: assets/app.ico with sizes 16, 24, 32, 48, 64, 128, 256.
param(
    [string]$Source = "$PSScriptRoot\..\wall-clock.png",
    [string]$Dest   = "$PSScriptRoot\..\assets\app.ico"
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dest)
$src  = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
$sizes = 16,24,32,48,64,128,256

# Encode each size as PNG bytes.
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode   = 'HighQuality'
    $g.SmoothingMode     = 'AntiAlias'
    $g.CompositingQuality= 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($src, 0, 0, $s, $s)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngs += ,[pscustomobject]@{ Size = $s; Bytes = $ms.ToArray() }
}
$src.Dispose()

# Build ICO: ICONDIR (6 bytes) + N * ICONDIRENTRY (16 bytes) + N * image data.
$n = $pngs.Count
$out = New-Object System.IO.MemoryStream
$bw  = New-Object System.IO.BinaryWriter $out
$bw.Write([uint16]0)    # reserved
$bw.Write([uint16]1)    # type = ICO
$bw.Write([uint16]$n)   # count
$offset = 6 + 16 * $n
foreach ($p in $pngs) {
    $w = if ($p.Size -ge 256) { 0 } else { [byte]$p.Size }
    $h = $w
    $bw.Write([byte]$w)
    $bw.Write([byte]$h)
    $bw.Write([byte]0)        # color palette
    $bw.Write([byte]0)        # reserved
    $bw.Write([uint16]1)      # color planes
    $bw.Write([uint16]32)     # bpp
    $bw.Write([uint32]$p.Bytes.Length)
    $bw.Write([uint32]$offset)
    $offset += $p.Bytes.Length
}
foreach ($p in $pngs) { $bw.Write($p.Bytes) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($Dest, $out.ToArray())
"$Dest ($($out.Length) bytes)"
