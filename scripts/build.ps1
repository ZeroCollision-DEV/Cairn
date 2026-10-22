<#
  Builds Cairn (cairn.dll) and Cairn Injector (CairnInjector.exe) with MSVC x64 and installs them.

  Usage (from the project folder):
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -JavaHome "<64-bit JDK folder>"

  Parameters
    -JavaHome   JDK folder (contains include\jni.h and include\win32\jni_md.h). Defaults to %JAVA_HOME%.
    -OutDir     Install folder. Default: <Desktop>\Cairn (an older <Desktop>\YTClient package is never touched or deleted)
    -Generator  Optional CMake generator, e.g. "Visual Studio 17 2022" (default: CMake's newest installed Visual Studio)
    -BuildDir   Default: <project>\build-release (a fresh folder; other build folders are never reused or touched)
    -Tests      Also build and run the portable unit tests

  Exit code is non-zero if any step fails or an expected output file is missing.
#>
param(
    [string]$JavaHome = $env:JAVA_HOME,
    [string]$OutDir = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Cairn'),
    [string]$Generator = '',
    [string]$BuildDir = '',
    [switch]$Tests
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build-release' }

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Run($what, [scriptblock]$cmd) {
    Write-Host "== $what =="
    & $cmd
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit code $LASTEXITCODE). See the output above." }
}

# ── Validate prerequisites ───────────────────────────────────
if (-not [Environment]::Is64BitOperatingSystem) { Fail 'A 64-bit Windows is required.' }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Fail 'cmake was not found on PATH. Install CMake 3.20+ and reopen the terminal.' }
if (-not $JavaHome) { Fail 'No JDK given. Pass -JavaHome "<JDK folder>" or set JAVA_HOME.' }
if (-not (Test-Path -LiteralPath $JavaHome -PathType Container)) { Fail "JDK folder not found: $JavaHome" }
$jni = Join-Path $JavaHome 'include\jni.h'
$jniMd = Join-Path $JavaHome 'include\win32\jni_md.h'
if (-not (Test-Path -LiteralPath $jni)) { Fail "Not a JDK (missing $jni). A JRE is not enough." }
if (-not (Test-Path -LiteralPath $jniMd)) { Fail "Missing $jniMd. Use a 64-bit JDK for Windows." }
$javaExe = Join-Path $JavaHome 'bin\java.exe'
if (-not (Test-Path -LiteralPath $javaExe)) { Fail "Missing $javaExe. Use a full 64-bit JDK." }

# `java -version` prints to stderr. Redirecting a native command's stderr (2>&1) turns each line into a
# PowerShell error record, which throws under $ErrorActionPreference='Stop' in Windows PowerShell 5.1.
# Running it through System.Diagnostics.Process avoids that on both PowerShell 5.1 and 7.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $javaExe
$psi.Arguments = '-version'
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
try {
    $proc = [System.Diagnostics.Process]::Start($psi)
    $javaOut = ($proc.StandardError.ReadToEnd() + $proc.StandardOutput.ReadToEnd())
    $proc.WaitForExit()
    $javaExit = $proc.ExitCode
} catch {
    Fail "Could not run $javaExe : $($_.Exception.Message)"
}
if ($javaExit -ne 0) { Fail "java -version exited with code $javaExit : $javaOut" }
$verLine = ($javaOut -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -First 1)
if ($javaOut -notmatch '64-Bit') { Fail "This JDK does not report a 64-bit VM: $verLine" }
Write-Host "JDK: $JavaHome  ($verLine)"
if (Test-Path -LiteralPath $BuildDir) { Write-Host "Reusing build folder $BuildDir (delete it for a completely clean build)." }

# ── Configure / build / install ──────────────────────────────
$cfg = @('-S', $root, '-B', $BuildDir, '-A', 'x64', "-DJAVA_HOME=$($JavaHome -replace '\\', '/')", "-DCMAKE_INSTALL_PREFIX=$($OutDir -replace '\\', '/')")
if ($Tests) { $cfg += '-DCAIRN_BUILD_TESTS=ON' }
if ($Generator) { $cfg = @('-G', $Generator) + $cfg }

Run 'Configure'                    { cmake @cfg }
Run 'Build cairn'                 { cmake --build $BuildDir --config Release --target cairn --parallel }
Run 'Build cairn_injector'        { cmake --build $BuildDir --config Release --target cairn_injector --parallel }
if ($Tests) {
    Run 'Build portable_tests'     { cmake --build $BuildDir --config Release --target portable_tests }
    Run 'Run portable_tests'       { & (Join-Path $BuildDir 'Release\portable_tests.exe') }
}
# --prefix makes the destination explicit even if an older CMake cache remembers another install folder.
Write-Host "Install folder: $OutDir"
$legacyOut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'YTClient'
if ((Test-Path -LiteralPath $legacyOut) -and ($legacyOut -ne $OutDir)) {
    Write-Host "Note: an older package exists at $legacyOut. It is left untouched; the new build is installed only to $OutDir." -ForegroundColor Yellow
}
Run 'Install'                      { cmake --install $BuildDir --config Release --component Cairn --prefix $OutDir }

# ── Move pre-rename binaries out of the way (moved, never deleted) ──
# Older builds of this project were called ytclient.dll / YTInjector.exe. Leaving them next to the
# new files would look like part of the new build, so they are moved into a clearly named folder.
$stale = @('bin\ytclient.dll', 'bin\YTInjector.exe', 'assets\icons\ytclient.ico', 'assets\icons\ytinjector.ico')
$staleFound = $stale | Where-Object { Test-Path -LiteralPath (Join-Path $OutDir $_) }
if ($staleFound) {
    $parking = Join-Path $OutDir '_previous_ytclient_build'
    New-Item -ItemType Directory -Force -Path $parking | Out-Null
    foreach ($f in $staleFound) {
        $src = Join-Path $OutDir $f
        $dst = Join-Path $parking (Split-Path -Leaf $f)
        if (Test-Path -LiteralPath $dst) { $dst = Join-Path $parking ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + (Split-Path -Leaf $f)) }
        Move-Item -LiteralPath $src -Destination $dst
        Write-Host "Moved old-brand file: $f -> $parking"
    }
}

# ── Verify the outputs really exist ──────────────────────────
$expected = @('bin\cairn.dll', 'bin\CairnInjector.exe', 'config\injector.ini', 'README.md', 'THIRD_PARTY_NOTICES.md',
              'assets\icons\cairn.ico', 'assets\icons\cairn_injector.ico', 'assets\icons\logo.png', 'assets\icons\logo.svg',
              'assets\icons\wordmark.png', 'assets\sounds\click.wav', 'docs\DESIGN.md', 'docs\INJECTOR.md')
$missing = $expected | Where-Object { -not (Test-Path -LiteralPath (Join-Path $OutDir $_)) }
if ($missing) { Fail ("Build finished but these files are missing from ${OutDir}: " + ($missing -join ', ')) }

$leftover = $stale | Where-Object { Test-Path -LiteralPath (Join-Path $OutDir $_) }
if ($leftover) { Fail ("Old-brand files are still in ${OutDir}: " + ($leftover -join ', ')) }

# Architecture check: PE machine type must be 0x8664 (x64).
foreach ($f in 'bin\cairn.dll', 'bin\CairnInjector.exe') {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $OutDir $f))
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
    if ($machine -ne 0x8664) { Fail ("{0} is not an x64 binary (machine 0x{1:X4})." -f $f, $machine) }
}
foreach ($f in 'bin\cairn.dll', 'bin\CairnInjector.exe') {
    $fi = Get-Item -LiteralPath (Join-Path $OutDir $f)
    Write-Host ("  {0,-24} {1,10:N0} bytes  {2:yyyy-MM-dd HH:mm:ss}" -f $f, $fi.Length, $fi.LastWriteTime)
}
Write-Host "Done. Built, installed and checked (files present, x64): $OutDir" -ForegroundColor Green
Write-Host 'Note: this does not test injection or any in-game behaviour.'
