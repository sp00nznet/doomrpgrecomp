# Convert every frame*.ppm in a directory to a sibling .png (P6, any size).
param([Parameter(Mandatory=$true)][string]$Dir)
Add-Type -AssemblyName System.Drawing
foreach ($f in (Get-ChildItem $Dir -Filter *.ppm | Sort-Object Name)) {
    $b = [System.IO.File]::ReadAllBytes($f.FullName)
    # Parse "P6\n<w> <h>\n<maxval>\n" header (whitespace-separated tokens).
    $p = 2; $vals = @()
    while ($vals.Count -lt 3) {
        while ($b[$p] -eq 32 -or $b[$p] -eq 9 -or $b[$p] -eq 10 -or $b[$p] -eq 13) { $p++ }
        $s = $p
        while (-not ($b[$p] -eq 32 -or $b[$p] -eq 9 -or $b[$p] -eq 10 -or $b[$p] -eq 13)) { $p++ }
        $vals += [int][string]([System.Text.Encoding]::ASCII.GetString($b[$s..($p-1)]))
    }
    $p++   # single whitespace after maxval
    $w = $vals[0]; $h = $vals[1]
    $bmp = New-Object System.Drawing.Bitmap($w, $h); $idx = $p
    for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) {
        $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($b[$idx], $b[$idx+1], $b[$idx+2])); $idx += 3
    } }
    $out = [System.IO.Path]::ChangeExtension($f.FullName, '.png')
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
Write-Host "converted" (Get-ChildItem $Dir -Filter *.png).Count "frames in" $Dir
