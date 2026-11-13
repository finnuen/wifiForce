# =============================================================================
#  wifiForce - builds app.ico from geometry
#
#  The icon is a Wi-Fi symbol inside a shield, drawn on the same rounded-square
#  blue tile JustViewMD uses, so the two applications look like a family:
#
#    tile       rounded square, corner radius 0.252, vertical gradient
#               #518CF2 -> #3F64D9 (JustViewMD's app tile)
#    shield     white, filled, a pointed crest
#    Wi-Fi      cut out of the shield, so the gradient shows through the mark
#
#  Every size is drawn from these fractions rather than scaled down from one
#  bitmap, so the 16 px frame is a deliberate drawing and not grey mush:
#
#    * 16-48 px are stored as uncompressed DIBs, because parts of the shell
#      still refuse the PNG variant at small sizes,
#    * 64 px and up are stored as PNG, which keeps the file small and the
#      gradients clean.
#
#  Usage:
#    powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
#    powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1 -PreviewDir C:\temp\wififorce-icon
# =============================================================================

[CmdletBinding()]
param(
    [string] $OutputPath,
    [string] $PreviewDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot     = Split-Path -Parent $scriptDirectory
if (-not $OutputPath) { $OutputPath = Join-Path $projectRoot 'app.ico' }

$sizes = @(16, 24, 32, 48, 64, 128, 256)

# --- the tile and the mark, as fractions of the tile's side -------------------
$tileRadius      = 0.252
$tileTopColor    = [System.Drawing.Color]::FromArgb(255, 0x51, 0x8C, 0xF2)
$tileBottomColor = [System.Drawing.Color]::FromArgb(255, 0x3F, 0x64, 0xD9)

$shieldWidth     = 0.62      # of the tile
$shieldTop       = 0.170     # of the tile
$shieldBottom    = 0.845
$shieldShoulder  = 0.42      # where the straight sides start curving in

$markCentre      = 0.60      # WiFi origin, down from the shield's top
$arcInner        = 0.125     # arc radii, of the shield's width
$arcMiddle       = 0.245
$arcOuter        = 0.365
$arcStroke       = 0.062
$dotRadius       = 0.075

function New-RoundedTilePath {
    param([int] $Size)

    $diameter = $tileRadius * 2.0 * $Size
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $diameter, $diameter, 180, 90)
    $path.AddArc($Size - $diameter, 0, $diameter, $diameter, 270, 90)
    $path.AddArc($Size - $diameter, $Size - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc(0, $Size - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-ShieldPath {
    param(
        [int] $Size,
        [double] $WidthFraction = $shieldWidth
    )

    $width  = $WidthFraction * $Size
    $left   = ($Size - $width) / 2.0
    $right  = $left + $width
    $top    = $shieldTop * $Size
    $bottom = $shieldBottom * $Size
    $height = $bottom - $top
    $centre = 0.5 * $Size
    $shoulder = $top + $shieldShoulder * $height

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddLine([single] $left,  [single] $top, [single] $right, [single] $top)
    $path.AddLine([single] $right, [single] $top, [single] $right, [single] $shoulder)
    # Right side: bulge out, then sweep in to the point.
    $path.AddBezier(
        [single] $right,  [single] $shoulder,
        [single] $right,  [single] ($shoulder + 0.34 * $height),
        [single] ($centre + 0.30 * $width), [single] ($bottom - 0.26 * $height),
        [single] $centre, [single] $bottom)
    $path.AddBezier(
        [single] $centre, [single] $bottom,
        [single] ($centre - 0.30 * $width), [single] ($bottom - 0.26 * $height),
        [single] $left,   [single] ($shoulder + 0.34 * $height),
        [single] $left,   [single] $shoulder)
    $path.AddLine([single] $left, [single] $shoulder, [single] $left, [single] $top)
    $path.CloseFigure()
    return $path
}

function New-IconBitmap {
    param([int] $Size)

    # Below about 24 px the true stroke weights fall under one pixel, so the
    # shield is widened a little, the mark is fattened, and the innermost arc
    # is dropped: three hairlines in a 10 px shield read as a smudge, two read
    # as Wi-Fi.
    $small    = $Size -le 24
    $boost    = if ($Size -le 16) { 1.75 } elseif ($small) { 1.45 } else { 1.0 }
    $fraction = if ($small) { 0.68 } else { $shieldWidth }
    # The parenthesis around each element are load bearing: the comma binds
    # tighter than the multiplication in PowerShell.
    $arcs     = if ($small) { @(($arcMiddle * 1.40), ($arcOuter * 1.40)) }
                else        { @($arcInner, $arcMiddle, $arcOuter) }
    $minStroke = if ($Size -le 16) { 1.25 } else { 1.0 }
    $minDot    = if ($Size -le 16) { 1.0 } else { 0.85 }
    $stroke   = [Math]::Max($minStroke, $arcStroke * $boost * $fraction * $Size)
    $dot      = [Math]::Max($minDot, $dotRadius * ($boost - 0.25) * $fraction * $Size)

    $bitmap = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $tile = $null; $shield = $null; $tileBrush = $null; $white = $null; $pen = $null
    try {
        $graphics.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.Clear([System.Drawing.Color]::Transparent)

        # --- the tile ---------------------------------------------------------
        $tile = New-RoundedTilePath -Size $Size
        $tileRect = New-Object System.Drawing.Rectangle(0, 0, $Size, $Size)
        $tileBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
            $tileRect, $tileTopColor, $tileBottomColor,
            [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
        $graphics.FillPath($tileBrush, $tile)

        # --- the shield -------------------------------------------------------
        $shield = New-ShieldPath -Size $Size -WidthFraction $fraction
        $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
        $graphics.FillPath($white, $shield)

        # --- the Wi-Fi mark, cut out of the shield ----------------------------
        # Inside the shield's outline there is no tile to show through, so the
        # mark is drawn with the same gradient the tile has: it reads as a hole
        # punched in the white crest.
        $graphics.SetClip($shield)
        $markBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
            $tileRect, $tileTopColor, $tileBottomColor,
            [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
        $pen = New-Object System.Drawing.Pen($markBrush, [single] $stroke)
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

        $centreX = 0.5 * $Size
        $centreY = $shieldTop * $Size + $markCentre * ($shieldBottom - $shieldTop) * $Size
        $width   = $fraction * $Size

        foreach ($radius in $arcs) {
            $r = $radius * $width
            $arcRect = New-Object System.Drawing.RectangleF(
                ([single] ($centreX - $r)), ([single] ($centreY - $r)),
                ([single] (2.0 * $r)), ([single] (2.0 * $r)))
            $graphics.DrawArc($pen, $arcRect, 205, 130)
        }
        $graphics.FillEllipse($markBrush, ([single] ($centreX - $dot)), ([single] ($centreY - $dot)),
            ([single] (2.0 * $dot)), ([single] (2.0 * $dot)))
        $graphics.ResetClip()
    } finally {
        if ($pen) { $pen.Dispose() }
        if ($markBrush) { $markBrush.Dispose() }
        if ($white) { $white.Dispose() }
        if ($shield) { $shield.Dispose() }
        if ($tileBrush) { $tileBrush.Dispose() }
        if ($tile) { $tile.Dispose() }
        $graphics.Dispose()
    }
    return $bitmap
}

function Get-BitmapBytes {
    param([System.Drawing.Bitmap] $Bitmap)

    $rect = New-Object System.Drawing.Rectangle(0, 0, $Bitmap.Width, $Bitmap.Height)
    $data = $Bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $stride = $data.Stride
        $bytes  = New-Object byte[] ($stride * $Bitmap.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    } finally {
        $Bitmap.UnlockBits($data)
    }

    # GDI+ hands back premultiplied BGRA; ICO wants straight BGRA. Undo it.
    for ($i = 0; $i -lt $bytes.Length; $i += 4) {
        $alpha = $bytes[$i + 3]
        if ($alpha -eq 0) {
            $bytes[$i] = 0; $bytes[$i + 1] = 0; $bytes[$i + 2] = 0
        } elseif ($alpha -ne 255) {
            foreach ($channel in 0, 1, 2) {
                $value = [int] $bytes[$i + $channel] * 255
                $bytes[$i + $channel] = [byte] [Math]::Min(255, [int] (($value + $alpha / 2) / $alpha))
            }
        }
    }
    return ,$bytes
}

function ConvertTo-DibBytes {
    param(
        [System.Drawing.Bitmap] $Bitmap,
        [int] $Size
    )

    $bgra   = Get-BitmapBytes -Bitmap $Bitmap
    $stride = $Size * 4

    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter($stream)

    # BITMAPINFOHEADER: height counts the XOR image plus the AND mask.
    $writer.Write([uint32] 40)
    $writer.Write([int32]  $Size)
    $writer.Write([int32]  ($Size * 2))
    $writer.Write([uint16] 1)
    $writer.Write([uint16] 32)
    $writer.Write([uint32] 0)             # BI_RGB
    $writer.Write([uint32] ($stride * $Size))
    $writer.Write([int32]  0)             # pixels per metre
    $writer.Write([int32]  0)
    $writer.Write([uint32] 0)
    $writer.Write([uint32] 0)

    for ($y = $Size - 1; $y -ge 0; --$y) {
        $writer.Write($bgra, ($y * $stride), $stride)
    }

    # AND mask: all zero (the alpha channel already describes the shape).
    $maskStride = [int] ([Math]::Floor(($Size + 31) / 32) * 4)
    $maskRow    = New-Object byte[] $maskStride
    for ($y = 0; $y -lt $Size; ++$y) {
        $writer.Write($maskRow, 0, $maskStride)
    }

    $writer.Flush()
    $result = $stream.ToArray()
    $writer.Dispose()
    $stream.Dispose()
    return ,$result
}

function ConvertTo-PngBytes {
    param([System.Drawing.Bitmap] $Bitmap)

    $stream = New-Object System.IO.MemoryStream
    $Bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $result = $stream.ToArray()
    $stream.Dispose()
    return ,$result
}

# --- build every frame --------------------------------------------------------

$frames = @()
foreach ($size in $sizes) {
    $bitmap = New-IconBitmap -Size $size
    try {
        if ($PreviewDir) {
            if (-not (Test-Path -LiteralPath $PreviewDir)) {
                New-Item -ItemType Directory -Path $PreviewDir -Force | Out-Null
            }
            $bitmap.Save((Join-Path $PreviewDir "icon-$size.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        }
        if ($size -le 48) {
            $bytes = ConvertTo-DibBytes -Bitmap $bitmap -Size $size
        } else {
            $bytes = ConvertTo-PngBytes -Bitmap $bitmap
        }
        $frames += [pscustomobject]@{ Size = $size; Bytes = $bytes }
    } finally {
        $bitmap.Dispose()
    }
}

# --- assemble the .ico container ---------------------------------------------

$output = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($output)

$writer.Write([uint16] 0)                 # reserved
$writer.Write([uint16] 1)                 # type: icon
$writer.Write([uint16] $frames.Count)

$offset = 6 + 16 * $frames.Count
foreach ($frame in $frames) {
    $dimension = if ($frame.Size -ge 256) { 0 } else { $frame.Size }
    $writer.Write([byte]   $dimension)
    $writer.Write([byte]   $dimension)
    $writer.Write([byte]   0)             # palette size
    $writer.Write([byte]   0)             # reserved
    $writer.Write([uint16] 1)             # colour planes
    $writer.Write([uint16] 32)            # bits per pixel
    $writer.Write([uint32] $frame.Bytes.Length)
    $writer.Write([uint32] $offset)
    $offset += $frame.Bytes.Length
}
foreach ($frame in $frames) {
    $writer.Write($frame.Bytes, 0, $frame.Bytes.Length)
}

$writer.Flush()
$bytes = $output.ToArray()
$writer.Dispose()
$output.Dispose()

$directory = Split-Path -Parent $OutputPath
if ($directory -and -not (Test-Path -LiteralPath $directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
[System.IO.File]::WriteAllBytes($OutputPath, $bytes)

# --- report -------------------------------------------------------------------

$icon = New-Object System.Drawing.Icon($OutputPath)
try {
    $described = ($sizes -join ', ')
    Write-Host ("Wrote {0} ({1} bytes, {2} frames: {3})" -f $OutputPath, $bytes.Length, $frames.Count, $described)
} finally {
    $icon.Dispose()
}
