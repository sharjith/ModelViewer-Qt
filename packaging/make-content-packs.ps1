<#
.SYNOPSIS
  Builds the optional ModelViewer content packs (zip files) that are published next to a release instead of being inside the installer / .deb.

.DESCRIPTION
  The installer and the .deb ship only a small essentials set of materials and HDRI environment maps (see the install rules in CMakeLists.txt).
  The complete libraries are separate downloads, each under GitHub's 2 GiB limit per release asset:

    ModelViewer-MaterialLibrary-<version>-part1.zip, -part2.zip   every material preset (textures/materials/...), split alphabetically
    ModelViewer-EnvironmentMaps-<version>.zip                      every HDRI pack (textures/envmap/skyboxes/HDRI/...)
    ModelViewer-LargeSamples-<version>.zip                         the generated stress-test result files (sample-models/Simulation/large), when present

  Every zip holds paths that start at "textures\..." or "sample-models\..." so that it is unzipped INTO the data folder:
    Windows : <install folder>\share\ModelViewer\   (default C:\Program Files\ModelViewer\share\ModelViewer\; needs an administrator)
    Linux   : /usr/share/ModelViewer/               (sudo)
  and each zip carries a README.txt with these instructions. A SHA256SUMS.txt is written next to the zips.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File packaging\make-content-packs.ps1 -Version 2026.11.0 -OutDir dist\content-packs
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$OutDir = 'dist\content-packs',
    [string]$RepoRoot = ''
)

if (-not $RepoRoot) {
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $RepoRoot = (Resolve-Path (Join-Path $here '..')).Path
}

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$limit = 1.9GB   # stay under GitHub's 2 GiB per asset
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

function New-Pack([string]$zipPath, [string[]]$sourceDirs, [string]$title, [string]$unzipInto) {
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    $readme = @"
$title
$('=' * $title.Length)

This is an optional content pack for ModelViewer $Version. Unzip it INTO the ModelViewer data folder, so that the folder named in this archive
(textures\... or sample-models\...) lands next to the ones already there:

  Windows : <install folder>\share\ModelViewer\      (default C:\Program Files\ModelViewer\share\ModelViewer\ - needs administrator rights)
  Linux   : /usr/share/ModelViewer/                  (sudo unzip -o <file> -d /usr/share/ModelViewer)

$unzipInto

Existing files are overwritten with identical content. Restart ModelViewer afterwards. The checksums are in SHA256SUMS.txt next to the download.
"@
    $zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $entry = $zip.CreateEntry('README.txt'); $w = New-Object System.IO.StreamWriter($entry.Open()); $w.Write($readme); $w.Dispose()
        foreach ($dir in $sourceDirs) {
            $base = $RepoRoot.TrimEnd('\') + '\'
            foreach ($f in Get-ChildItem -LiteralPath $dir -Recurse -File -Force) {
                $rel = $f.FullName.Substring($base.Length).Replace('\', '/')
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $rel, [System.IO.Compression.CompressionLevel]::Fastest)
            }
        }
    } finally { $zip.Dispose() }
    $mb = [math]::Round((Get-Item $zipPath).Length / 1MB)
    Write-Host ("{0}  {1} MB" -f (Split-Path $zipPath -Leaf), $mb)
    if ((Get-Item $zipPath).Length -gt $limit) { throw "$zipPath is over the 2 GiB release-asset limit; split it further." }
}

# 1. Material library: every preset folder, split into two roughly equal halves by size, alphabetically
$materialsRoot = Join-Path $RepoRoot 'textures\materials'
$presets = Get-ChildItem -LiteralPath $materialsRoot -Directory | Sort-Object Name | ForEach-Object {
    [pscustomobject]@{ Dir = $_.FullName; Bytes = (Get-ChildItem -LiteralPath $_.FullName -Recurse -File | Measure-Object Length -Sum).Sum }
}
$total = ($presets | Measure-Object Bytes -Sum).Sum
$part1 = @(); $part2 = @(); $acc = 0
foreach ($p in $presets) { if ($acc -lt $total / 2) { $part1 += $p.Dir } else { $part2 += $p.Dir }; $acc += $p.Bytes }
New-Pack (Join-Path $OutDir "ModelViewer-MaterialLibrary-$Version-part1.zip") $part1 "ModelViewer material library, part 1 of 2" "Part 1 holds textures\materials\ presets from the start of the alphabet; part 2 holds the rest. Unzip both."
New-Pack (Join-Path $OutDir "ModelViewer-MaterialLibrary-$Version-part2.zip") $part2 "ModelViewer material library, part 2 of 2" "Part 2 holds textures\materials\ presets from the end of the alphabet; part 1 holds the rest. Unzip both."

# 2. Environment maps
New-Pack (Join-Path $OutDir "ModelViewer-EnvironmentMaps-$Version.zip") @((Join-Path $RepoRoot 'textures\envmap\skyboxes\HDRI')) "ModelViewer HDRI environment maps" "This adds every HDRI pack to textures\envmap\skyboxes\HDRI; they appear in the Environment panel's preset list."

# 3. Large samples (generated locally, git-ignored)
$large = Join-Path $RepoRoot 'sample-models\Simulation\large'
if ((Get-ChildItem -LiteralPath $large -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.exo', '.cgns', '.frd', '.vtu' })) {
    New-Pack (Join-Path $OutDir "ModelViewer-LargeSamples-$Version.zip") @($large) "ModelViewer large simulation samples" "These are stress-test results (about a million cells) for the lazy-loading readers; see sample-models\Simulation\large\README.txt."
} else { Write-Host 'No generated large samples found; run sample-models\Simulation\large\make_large_samples.py to create them. Skipping that pack.' }

# Checksums
$sums = Get-ChildItem -LiteralPath $OutDir -Filter 'ModelViewer-*-*.zip' | Get-FileHash -Algorithm SHA256 | ForEach-Object { '{0}  {1}' -f $_.Hash.ToLower(), (Split-Path $_.Path -Leaf) }
Set-Content -Path (Join-Path $OutDir 'SHA256SUMS.txt') -Value $sums -Encoding ascii
Write-Host 'SHA256SUMS.txt written'
