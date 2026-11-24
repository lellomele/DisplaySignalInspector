$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-RoundedRectangle([float]$X, [float]$Y, [float]$Width, [float]$Height, [float]$Radius) {
    $path = [Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = 2 * $Radius
    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc(($X + $Width - $diameter), $Y, $diameter, $diameter, 270, 90)
    $path.AddArc(($X + $Width - $diameter), ($Y + $Height - $diameter), $diameter, $diameter, 0, 90)
    $path.AddArc($X, ($Y + $Height - $diameter), $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

$background = New-RoundedRectangle 8 8 240 240 49
$leftMonitor = New-RoundedRectangle 37 59 115 81 13
$rightMonitor = New-RoundedRectangle 103 104 118 78 13
$blue = [Drawing.ColorTranslator]::FromHtml('#185BA2')
$navy = [Drawing.ColorTranslator]::FromHtml('#122544')
$backgroundFill = [Drawing.Drawing2D.LinearGradientBrush]::new(
    [Drawing.RectangleF]::new(8, 8, 240, 240), $blue, $navy, 50.0)
$blueScreen = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#70B9FF'))
$greenScreen = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#60E8BF'))
$outline = [Drawing.Pen]::new($navy, 7)
$stand = [Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml('#FFFFFF'), 10)
$stand.StartCap = [Drawing.Drawing2D.LineCap]::Round
$stand.EndCap = [Drawing.Drawing2D.LineCap]::Round

$sizes = @(16, 24, 32, 48, 64, 256)
$images = [Collections.Generic.List[byte[]]]::new()
try {
    foreach ($size in $sizes) {
        $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.ScaleTransform($size / 256.0, $size / 256.0)
            $graphics.FillPath($backgroundFill, $background)
            $graphics.DrawLine($stand, 79, 138, 79, 153)
            $graphics.DrawLine($stand, 63, 154, 95, 154)
            $graphics.FillPath($blueScreen, $leftMonitor)
            $graphics.DrawLine($stand, 162, 181, 162, 201)
            $graphics.DrawLine($stand, 141, 202, 183, 202)
            $graphics.FillPath($greenScreen, $rightMonitor)
            $graphics.DrawPath($outline, $rightMonitor)
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $images.Add($stream.ToArray())
        } finally {
            $stream.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }

    $output = [IO.File]::Create((Join-Path $PSScriptRoot 'schermo.ico'))
    $writer = [IO.BinaryWriter]::new($output)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $writer.Write([byte]($sizes[$i] % 256))
            $writer.Write([byte]($sizes[$i] % 256))
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$i].Length)
            $writer.Write([uint32]$offset)
            $offset += $images[$i].Length
        }
        foreach ($image in $images) {
            $writer.Write($image)
        }
    } finally {
        $writer.Dispose()
    }
} finally {
    foreach ($item in @($stand, $outline, $greenScreen, $blueScreen, $backgroundFill, $rightMonitor,
                         $leftMonitor, $background)) {
        $item.Dispose()
    }
}
