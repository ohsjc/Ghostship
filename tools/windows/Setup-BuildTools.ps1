# One-time setup of everything needed to build Ghostship.exe on Windows.
# Safe to re-run: anything already installed is skipped.

$ErrorActionPreference = 'Stop'

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Requesting administrator rights..." -ForegroundColor Yellow
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -NoExit -File `"$PSCommandPath`""
    exit
}

$Total = 6
function Step($n, $text) { Write-Host "`n[$n/$Total] $text" -ForegroundColor Cyan }
function Ok($text) { Write-Host "  OK   $text" -ForegroundColor Green }
function Info($text) { Write-Host "  ...  $text" }

function Update-SessionPath {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Install-WingetPackage($id, [string[]]$extra = @()) {
    Info "Installing $id with winget (this can take a while)..."
    winget install --id $id -e --source winget --accept-package-agreements --accept-source-agreements @extra
    # 0x8A15002B = no applicable upgrade, 0x8A150061 = already installed
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189 -and $LASTEXITCODE -ne -1978335135) {
        throw "winget failed to install $id (exit code $LASTEXITCODE)"
    }
    Update-SessionPath
}

function Get-RealPython {
    # The python.exe in WindowsApps is only a Microsoft Store shortcut, not a real install.
    if (Get-Command py -ErrorAction SilentlyContinue) {
        $exe = & py -3.12 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and $exe) { return $exe }
    }
    return $null
}

function Get-VsWithCpp {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($path) { return ($path | Select-Object -First 1) }
    return $null
}

Write-Host "Ghostship Windows build setup" -ForegroundColor White
Write-Host "Downloads roughly 8 GB the first time. Leave this window open until it says DONE."

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget was not found. Install 'App Installer' from the Microsoft Store, then run this again."
}

Step 1 "Git"
Update-SessionPath
if (Get-Command git -ErrorAction SilentlyContinue) { Ok (git --version) }
else { Install-WingetPackage 'Git.Git'; Ok (git --version) }
git config --system core.longpaths true

Step 2 "Python 3.12"
$python = Get-RealPython
if (-not $python) {
    Install-WingetPackage 'Python.Python.3.12'
    $python = Get-RealPython
    if (-not $python) { throw "Python 3.12 install finished but 'py -3.12' does not work. Re-run this script." }
}
Ok "Python 3.12 at $python"

Step 3 "CMake"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Install-WingetPackage 'Kitware.CMake' }
Ok ((cmake --version) | Select-Object -First 1)

Step 4 "Visual Studio 2022 Build Tools (C++ compiler, v143, Windows SDK)"
$vs = Get-VsWithCpp
if (-not $vs) {
    Info "A Visual Studio Installer window will show progress (usually 10-30 minutes)."
    Install-WingetPackage 'Microsoft.VisualStudio.2022.BuildTools' @(
        '--override', '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
    $vs = Get-VsWithCpp
    if (-not $vs) { throw "Build Tools installed but the C++ tools were not found. Re-run this script." }
}
Ok "Found at $vs"

Step 5 "Long path support (needed by vcpkg)"
Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -Value 1 -Type DWord
Ok "LongPathsEnabled = 1"

Step 6 "Optional: Windows Defender exclusion for build folders (makes builds much faster)"
$projects = Join-Path $env:USERPROFILE 'Projects'
$answer = Read-Host "  Exclude $projects\Ghostship-src and $projects\vcpkg from real-time scanning? (y/n)"
if ($answer -match '^[yY]') {
    Add-MpPreference -ExclusionPath (Join-Path $projects 'Ghostship-src'), (Join-Path $projects 'vcpkg')
    Ok "Exclusions added"
} else {
    Info "Skipped"
}

Write-Host "`nDONE. Close this window, then run 2-Build-Ghostship.cmd." -ForegroundColor Green
