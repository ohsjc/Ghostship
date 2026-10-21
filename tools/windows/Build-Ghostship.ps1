# Builds Ghostship.exe locally and installs it into the game folder.
#
# Examples (from PowerShell, or pass the same flags to 2-Build-Ghostship.cmd):
#   .\Build-Ghostship.ps1                  # build the photo-mode branch from the WSL repo
#   .\Build-Ghostship.ps1 -Source fork     # build from github.com/ohsjc/Ghostship instead
#   .\Build-Ghostship.ps1 -Run             # build, install, then launch the game
#   .\Build-Ghostship.ps1 -Clean           # wipe the build folder first (if a build gets stuck)

param(
    [ValidateSet('wsl', 'fork')][string]$Source = 'wsl',
    [string]$Branch = 'photo-mode',
    [string]$SrcDir = (Join-Path $env:USERPROFILE 'Projects\Ghostship-src'),
    [string]$GameDir = (Join-Path $env:USERPROFILE 'Projects\Ghostship'),
    [switch]$Clean,
    [switch]$NoDeploy,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'

$WslRepo = '//wsl.localhost/Ubuntu/home/jeremy/projects/Ghostship'
$ForkRepo = 'https://github.com/ohsjc/Ghostship.git'
$BuildDir = Join-Path $SrcDir 'build\x64'
# Outside the build folder so -Clean doesn't throw away the compiled dependencies.
$env:VCPKG_ROOT = Join-Path $env:USERPROFILE 'Projects\vcpkg'

$Total = 6
$timer = [Diagnostics.Stopwatch]::StartNew()
function Step($n, $text) { Write-Host "`n[$n/$Total] $text  ($([int]$timer.Elapsed.TotalMinutes) min elapsed)" -ForegroundColor Cyan }
function Ok($text) { Write-Host "  OK   $text" -ForegroundColor Green }
function Check($what) { if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" } }

$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path', 'User')

Step 1 "Checking tools"
foreach ($tool in 'git', 'cmake', 'py') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "'$tool' not found. Run 1-Setup-Build-Tools.cmd first."
    }
}
$python = & py -3.12 -c "import sys; print(sys.executable)"
Check "Python 3.12 (run 1-Setup-Build-Tools.cmd)"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere) -or -not (& $vswhere -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)) {
    throw "Visual Studio 2022 C++ tools not found. Run 1-Setup-Build-Tools.cmd first."
}
Ok "git, cmake, Python 3.12, Visual Studio 2022 C++"

Step 2 "Getting source ($Source / $Branch)"
if (-not (Test-Path (Join-Path $SrcDir '.git'))) {
    git clone -c core.autocrlf=false -c core.longpaths=true $ForkRepo $SrcDir
    Check "git clone"
}
Push-Location $SrcDir
try {
    if ((git remote) -notcontains 'wsl') { git remote add wsl $WslRepo }
    $remote = 'origin'
    if ($Source -eq 'wsl') { $remote = 'wsl' }
    # The WSL repo is owned by the Linux user, so git needs permission to read it.
    git -c safe.directory=* fetch $remote $Branch
    Check "git fetch from $remote (is WSL running and the branch committed?)"
    # The Windows copy is build-only: always match the fetched branch exactly.
    git checkout -f -B $Branch FETCH_HEAD
    Check "git checkout"
    git submodule update --init --recursive
    Check "git submodule update"
    Ok "Building commit: $(git log --oneline -1)"

    Step 3 "Python packages"
    & py -3.12 -m pip install --user --quiet --disable-pip-version-check -r libultraship\requirements.txt
    Check "pip install"
    Ok "requirements installed"

    Step 4 "Configuring CMake (first run downloads and builds vcpkg libraries: 20-40 min)"
    if ($Clean -and (Test-Path $BuildDir)) {
        Remove-Item -Recurse -Force $BuildDir
        Ok "Removed old build folder"
    }
    # Re-run every time so newly added source files are picked up.
    cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -T v143 -A x64 "-DPython3_EXECUTABLE=$python"
    Check "CMake configure"
    Ok "configured"

    Step 5 "Compiling (first build ~15-30 min, later builds a few minutes)"
    cmake --build $BuildDir --config Release --target GeneratePortO2R --parallel
    Check "Building ghostship.o2r"
    cmake --build $BuildDir --config Release --target Ghostship --parallel
    Check "Building Ghostship.exe"

    $exe = Join-Path $BuildDir 'Release\Ghostship.exe'
    if (-not (Test-Path $exe)) { $exe = Join-Path $BuildDir 'Ghostship.exe' }
    if (-not (Test-Path $exe)) { throw "Build finished but Ghostship.exe was not found under $BuildDir" }
    $outDir = Split-Path $exe
    Ok "Built $exe"

    # Keep the wizard copy in sync with the version in the repo.
    $repoTools = Join-Path $SrcDir 'tools\windows'
    if ((Resolve-Path $PSScriptRoot).Path -ne (Resolve-Path $repoTools).Path) {
        Copy-Item (Join-Path $repoTools '*') $PSScriptRoot -Force
    }
} finally {
    Pop-Location
}

if ($NoDeploy) {
    Write-Host "`nDONE in $([int]$timer.Elapsed.TotalMinutes) min (not installed: -NoDeploy). Output: $outDir" -ForegroundColor Green
    exit 0
}

Step 6 "Installing into $GameDir"
if (Get-Process -Name Ghostship -ErrorAction SilentlyContinue) {
    throw "Ghostship is running. Close the game and run this again."
}
New-Item -ItemType Directory -Force $GameDir | Out-Null
$gameExe = Join-Path $GameDir 'Ghostship.exe'
foreach ($name in 'Ghostship.exe', 'ghostship.o2r') {
    $file = Join-Path $GameDir $name
    $backup = Join-Path $GameDir ([IO.Path]::GetFileNameWithoutExtension($name) + '-original' + [IO.Path]::GetExtension($name))
    if ((Test-Path $file) -and -not (Test-Path $backup)) {
        Copy-Item $file $backup
        Ok "Backed up the original $name"
    }
}
Copy-Item $exe $GameDir -Force
Get-ChildItem $outDir -Filter *.dll | Copy-Item -Destination $GameDir -Force
$tcc = Join-Path $outDir '.tcc'
if (Test-Path $tcc) { Copy-Item $tcc $GameDir -Recurse -Force }
$o2r = Join-Path $BuildDir 'ghostship.o2r'
if (-not (Test-Path $o2r)) { $o2r = Join-Path $SrcDir 'ghostship.o2r' }
Copy-Item $o2r $GameDir -Force
Ok "Installed Ghostship.exe, DLLs and ghostship.o2r"

Write-Host "`nDONE in $([int]$timer.Elapsed.TotalMinutes) min." -ForegroundColor Green
if ($Run) {
    Start-Process $gameExe -WorkingDirectory $GameDir
}
