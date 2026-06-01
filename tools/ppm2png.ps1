# Convert every frame*.ppm (and window*.ppm) in a directory to .png (P6, any size).
# Fast path via LockBits + Marshal.Copy.
param([Parameter(Mandatory=$true)][string]$Dir)
Add-Type -AssemblyName System.Drawing
$files = @(Get-ChildItem $Dir -Filter *.ppm)
foreach ($f in $files) {
    $b = [System.IO.File]::ReadAllBytes($f.FullName)
    $p = 2; $vals = @()
    while ($vals.Count -lt 3) {
        while ($b[$p] -eq 32 -or $b[$p] -eq 9 -or $b[$p] -eq 10 -or $b[$p] -eq 13) { $p++ }
        $s = $p
        while (-not ($b[$p] -eq 32 -or $b[$p] -eq 9 -or $b[$p] -eq 10 -or $b[$p] -eq 13)) { $p++ }
        $vals += [int][string]([System.Text.Encoding]::ASCII.GetString($b[$s..($p-1)]))
    }
    $p++; $w = $vals[0]; $h = $vals[1]
    $stride = $w * 3
    # Build a BGR bottom-up-agnostic buffer with row padding to 4 bytes.
    $pad = (4 - ($stride % 4)) % 4
    $rowLen = $stride + $pad
    $buf = New-Object byte[] ($rowLen * $h)
    for ($y = 0; $y -lt $h; $y++) {
        $src = $p + $y * $stride
        $dst = $y * $rowLen
        for ($x = 0; $x -lt $w; $x++) {
            $buf[$dst]   = $b[$src+2]   # B
            $buf[$dst+1] = $b[$src+1]   # G
            $buf[$dst+2] = $b[$src]     # R
            $src += 3; $dst += 3
        }
    }
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
    [System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $bd.Scan0, $buf.Length)
    $bmp.UnlockBits($bd)
    $bmp.Save([System.IO.Path]::ChangeExtension($f.FullName, '.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
Write-Host "converted" $files.Count "frames in" $Dir
