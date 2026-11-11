<#
    LEGO Star Wars: The Complete Saga Portable (LSTCS-P) - builder
    ==================================================================
    Turns your own copy of the Android game into the files the Vita needs:

      Ready For Vita\ux0\data\lswtcs\...   copy onto the Vita's ux0:

    The app itself is LSTCS-P.vpk from the GitHub release - install it with VitaShell.

    Run it through "LSTCS-P_Build.bat" in the folder above this one;
    it asks you to pick your game files in a normal Windows file window.
    Works on the PowerShell that ships with Windows 10/11; nothing to install.
#>
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# ---------------------------------------------------------------- settings --
$OutFolder    = 'Ready For Vita'
$VitaDataDir  = 'ux0\data\lswtcs'
$SoEntry      = 'lib/armeabi-v7a/libTTapp.so'
$SoSha1       = '291321330A3789414CAB7D411DAE64DEBC990AD6'   # Android v2.0.2.02 (build 20202)
# Name the game expects on the Vita  ->  names it may have on the phone
$DatFiles = [ordered]@{
    'Audio.dat'    = @('Audio.dat')
    'Levels.dat'   = @('Levels.dat')
    'Others.dat'   = @('Others.dat', 'Other.dat')
    'Textures.dat' = @('Textures.dat')
}
$ArchiveExts = @('.apk', '.apks', '.xapk', '.zip')

# ------------------------------------------------------------------ helpers --
function Say([string]$Text, [string]$Color = 'Gray') { Write-Host $Text -ForegroundColor $Color }
function Step([string]$Text) { Write-Host ''; Write-Host "== $Text" -ForegroundColor Cyan }
function Ok([string]$Text)   { Write-Host "   [OK]  $Text" -ForegroundColor Green }
function Warn([string]$Text) { Write-Host "   [!!]  $Text" -ForegroundColor Yellow }
function Fail([string]$Text) { Write-Host ''; Write-Host "   [X]   $Text" -ForegroundColor Red; throw 'stop' }

function Get-Sha1([string]$Path) { (Get-FileHash -Algorithm SHA1 -LiteralPath $Path).Hash.ToUpperInvariant() }

function Get-DatTarget([string]$Name) {
    foreach ($target in $DatFiles.Keys) {
        foreach ($alias in $DatFiles[$target]) {
            if ($Name -ieq $alias) { return $target }
        }
    }
    return $null
}

$Root     = (Resolve-Path -LiteralPath $Root).Path
$Compat   = Join-Path $Root 'compat'
$Work     = Join-Path $Compat '.work'
$Out      = Join-Path $Root $OutFolder
$OutData  = Join-Path $Out $VitaDataDir

$state = @{
    So         = $null    # path of a verified libTTapp.so
    WrongSo    = $null    # hash of a libTTapp.so from the wrong game version
    Arm64Only  = $false   # found only the 64-bit library
    Dats       = @{}      # target name -> extracted/loose path
    Encrypted  = @()      # .apkm bundles we can't open
}

# Looks inside an APK or a bundle of APKs (.apks/.xapk/.zip), one level of nesting.
function Search-Archive([string]$Path, [int]$Depth) {
    $zip = $null
    try {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
    } catch {
        Warn "Could not open $(Split-Path -Leaf $Path) as an archive - skipping it."
        return
    }
    try {
        foreach ($entry in $zip.Entries) {
            $full = $entry.FullName.Replace('\', '/')

            if ($full -ieq $SoEntry -and -not $state.So) {
                $dest = Join-Path $Work 'libTTapp.so'
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
                $hash = Get-Sha1 $dest
                if ($hash -eq $SoSha1) { $state.So = $dest } else { $state.WrongSo = $hash }
                continue
            }
            if ($full -ieq 'lib/arm64-v8a/libTTapp.so') { $state.Arm64Only = $true; continue }

            $target = Get-DatTarget $entry.Name
            if ($target -and -not $state.Dats.ContainsKey($target) -and $entry.Length -gt 0) {
                $dest = Join-Path $Work $target
                Say "      extracting $($entry.Name) from $(Split-Path -Leaf $Path) ..."
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
                $state.Dats[$target] = $dest
                continue
            }

            $ext = [System.IO.Path]::GetExtension($entry.Name).ToLowerInvariant()
            if ($Depth -lt 1 -and $ArchiveExts -contains $ext) {
                $nested = Join-Path $Work ('nested_' + [guid]::NewGuid().ToString('N') + $ext)
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $nested, $true)
                Search-Archive $nested ($Depth + 1)
                Remove-Item -LiteralPath $nested -Force -ErrorAction SilentlyContinue
            }
        }
    } finally {
        $zip.Dispose()
    }
}

# Shows a Windows "open file" window. Returns the chosen paths, an empty array if
# the player pressed Cancel, or $null if no window could be shown.
function Select-Files([string]$Title, [string]$Filter) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.Application]::EnableVisualStyles()
        $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false }
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Title = $Title
        $dialog.Filter = $Filter
        $dialog.Multiselect = $true
        $dialog.InitialDirectory = Join-Path $env:USERPROFILE 'Downloads'
        try {
            if ($dialog.ShowDialog($owner) -eq [System.Windows.Forms.DialogResult]::OK) { return ,@($dialog.FileNames) }
            return ,@()
        } finally {
            $dialog.Dispose(); $owner.Dispose()
        }
    } catch {
        return $null
    }
}

# Handles one file the player picked: an APK/bundle, libTTapp.so or a .dat file.
function Add-Input([string]$Path) {
    $name = [System.IO.Path]::GetFileName($Path)
    $ext = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($ext -eq '.apkm') { $state.Encrypted += $name; return }
    if ($ArchiveExts -contains $ext) {
        Say "   checking $name"
        Search-Archive $Path 0
        return
    }
    if ($name -ieq 'libTTapp.so' -and -not $state.So) {
        $hash = Get-Sha1 $Path
        if ($hash -eq $SoSha1) { $state.So = $Path } else { $state.WrongSo = $hash }
        return
    }
    $target = Get-DatTarget $name
    if ($target -and -not $state.Dats.ContainsKey($target)) { $state.Dats[$target] = $Path }
}

# --------------------------------------------------------------------- main --
try {
    Write-Host ''
    Write-Host '  LEGO Star Wars: The Complete Saga Portable' -ForegroundColor White
    Write-Host '  LSTCS-P builder' -ForegroundColor DarkGray

    if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
    New-Item -ItemType Directory -Path $Work | Out-Null

    # 1. Ask the player for their game files -----------------------------------
    Step 'Choose your game files'
    $picked = @()
    $game = Select-Files 'Select LEGO Star Wars: The Complete Saga (.apk, .apks or .xapk) - pick all split APKs if you have several' `
        'Android game (*.apk;*.apks;*.xapk;*.zip;*.apkm)|*.apk;*.apks;*.xapk;*.zip;*.apkm|All files (*.*)|*.*'
    if ($null -eq $game) {
        # No window available (e.g. run over SSH): fall back to files next to the .bat.
        Warn 'Could not open a file window - looking next to the .bat instead.'
        $skip = @($Compat, $Out, (Join-Path $Root '.git'))
        $picked = Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
            $p = $_.FullName
            -not ($skip | Where-Object { $p.StartsWith($_ + '\', [StringComparison]::OrdinalIgnoreCase) })
        } | ForEach-Object { $_.FullName }
    } elseif ($game.Count -eq 0) {
        Fail "No game selected. Run it again and choose your game's .apk (or .apks/.xapk) file."
    } else {
        $picked = $game
    }
    foreach ($path in $picked) { Add-Input $path }

    # The .dat files usually aren't inside the APK (Android downloads them later).
    $needDats = @($DatFiles.Keys | Where-Object { -not $state.Dats.ContainsKey($_) })
    if ($null -ne $game -and $state.So -and $needDats.Count -gt 0) {
        Say "   Your APK doesn't include $($needDats -join ', ')."
        Say '   Select them in the next window (you can pick all four at once), or press Cancel to skip.'
        $dats = Select-Files 'Select the game data files: Audio.dat, Levels.dat, Others.dat (or Other.dat), Textures.dat' `
            'Game data (*.dat)|*.dat|All files (*.*)|*.*'
        if ($dats) { foreach ($path in $dats) { Add-Input $path } }
    }

    # 2. Check the game library -------------------------------------------------
    Step 'Checking the game version'
    if (-not $state.So) {
        if ($state.WrongSo) {
            Fail ("Found libTTapp.so, but it's from a different version of the game (SHA1 $($state.WrongSo)).`n" +
                  "         This port needs the Android version 2.0.2.02 (build 20202).")
        }
        if ($state.Arm64Only) {
            Fail ("Your APK only contains the 64-bit game. The Vita needs the 32-bit (armeabi-v7a) build.`n" +
                  "         Dump the APK as a full bundle (.apks/.xapk) or from a 32-bit device, and drop it here.")
        }
        if ($state.Encrypted.Count -gt 0) {
            Fail ("$($state.Encrypted -join ', ') is an APKMirror .apkm bundle, which is encrypted.`n" +
                  "         Export it as .apks or .xapk instead (for example with SAI), and drop that here.")
        }
        Fail ("The game wasn't found in what you selected. Run it again and choose the game's .apk`n" +
              "         (or .apks/.xapk bundle). If your phone saved several split APKs, select them all.")
    }
    Ok 'Game library found and verified (v2.0.2.02)'

    # 3. Output folder ------------------------------------------------------------
    # The app itself is LSTCS-P.vpk from the GitHub release; this only prepares the
    # files it needs on the memory card. The port loads the game code from
    # ux0:data/lswtcs/libTTapp.so.
    Step 'Preparing the files for your Vita'
    if (Test-Path -LiteralPath $Out) {
        Say "   Clearing the old '$OutFolder' folder"
        Remove-Item -LiteralPath $Out -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $OutData | Out-Null
    Copy-Item -LiteralPath $state.So -Destination (Join-Path $OutData 'libTTapp.so')
    Ok 'Game code (libTTapp.so)'

    # 4. Game data ---------------------------------------------------------------
    Step 'Copying the game data'
    $missing = @()
    foreach ($target in $DatFiles.Keys) {
        if ($state.Dats.ContainsKey($target)) {
            $src = $state.Dats[$target]
            $dest = Join-Path $OutData $target
            if ($src.StartsWith($Work + '\', [StringComparison]::OrdinalIgnoreCase)) {
                Move-Item -LiteralPath $src -Destination $dest
            } else {
                Copy-Item -LiteralPath $src -Destination $dest
            }
            $mb = [math]::Round((Get-Item -LiteralPath $dest).Length / 1MB)
            Ok "$target ($mb MB)"
        } else {
            $missing += $target
        }
    }
    # Shaders: shipped as .cg and compiled on the Vita when the game first needs each one.
    $shaderZip = Join-Path $Compat 'shaders.zip'
    if (Test-Path -LiteralPath $shaderZip) {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($shaderZip)
        try {
            $count = 0
            foreach ($entry in $zip.Entries) {
                $name = $entry.FullName.Replace('\', '/')
                if (-not $name.StartsWith('cg/') -or $entry.Name -eq '') { continue }
                $dest = Join-Path $OutData ($name.Replace('/', '\'))
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
                $count++
            }
        } finally {
            $zip.Dispose()
        }
        Ok "Shaders ($count)"
    } else {
        Warn 'compat\shaders.zip is missing - the game will draw placeholder shaders.'
    }

    $defaultConfig = Join-Path $Compat 'config.txt'
    if (Test-Path -LiteralPath $defaultConfig) { Copy-Item -LiteralPath $defaultConfig -Destination (Join-Path $OutData 'config.txt') }

    $howTo = Join-Path $Compat 'HOW TO INSTALL.txt'
    if (Test-Path -LiteralPath $howTo) { Copy-Item -LiteralPath $howTo -Destination $Out }

    # 5. Done --------------------------------------------------------------------
    Write-Host ''
    if ($missing.Count -gt 0) {
        Warn ("Still missing: $($missing -join ', ')")
        Say  ("         The app is built, but the game won't start without these. Android downloads them`n" +
              "         after install, so they aren't inside the APK. Copy them off your phone (see README:`n" +
              "         'Getting the .dat files'), then run this again and select them when asked.") 'Yellow'
    } else {
        Ok 'Everything is ready.'
    }
    Write-Host ''
    Say "   Next: open '$OutFolder' and follow 'HOW TO INSTALL.txt'." 'White'
    $exitCode = 0
} catch {
    if ($_.Exception.Message -ne 'stop') {
        Write-Host ''
        Write-Host "   [X]   Something went wrong: $($_.Exception.Message)" -ForegroundColor Red
    }
    $exitCode = 1
} finally {
    if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue }
}
exit $exitCode
