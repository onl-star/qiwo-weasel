param(
  [string]$BoostVersion = "1.84.0",
  [string]$NsisVersion = "3.08",
  [string]$RimeTag = "",
  [switch]$NoDownloads,
  [switch]$NoInstaller
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WeaselRoot = Split-Path -Parent $ScriptDir
$RepoRoot = Split-Path -Parent $WeaselRoot
$OutputDir = Join-Path $WeaselRoot "output"
$DepsDir = Join-Path $WeaselRoot "deps"
$SevenZip = Join-Path $OutputDir "7z.exe"

function Write-Step([string]$Message) {
  Write-Host ""
  Write-Host "==> $Message"
}

function Download-File([string]$Uri, [string]$OutFile) {
  if ($NoDownloads) {
    throw "Download required but -NoDownloads was specified: $Uri"
  }
  if (Test-Path $OutFile) {
    Write-Host "Using cached $OutFile"
    return
  }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null
  Invoke-WebRequest -UserAgent "QiwoBuild" -Uri $Uri -OutFile $OutFile
}

function Expand-With7z([string]$Archive, [string]$Destination) {
  if (!(Test-Path $SevenZip)) {
    throw "7z.exe not found at $SevenZip"
  }
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  & $SevenZip x $Archive "-o$Destination" -y | Out-Host
  if ($LASTEXITCODE -ne 0) {
    throw "7z extraction failed: $Archive"
  }
}

function Get-VsDevCmd {
  $VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (!(Test-Path $VsWhere)) {
    throw "vswhere.exe not found. Install Visual Studio Build Tools with C++ workload."
  }
  $InstallPath = & $VsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
  if (!$InstallPath) {
    throw "Visual Studio Build Tools with MSBuild was not found."
  }
  $VsDevCmd = Join-Path $InstallPath "Common7\Tools\VsDevCmd.bat"
  if (!(Test-Path $VsDevCmd)) {
    throw "VsDevCmd.bat not found: $VsDevCmd"
  }
  return $VsDevCmd
}

function Ensure-Boost {
  $BoostName = "boost_" + $BoostVersion.Replace(".", "_")
  $BoostRoot = Join-Path $DepsDir $BoostName
  if (Test-Path (Join-Path $BoostRoot "boost")) {
    return $BoostRoot
  }

  Write-Step "Downloading Boost $BoostVersion"
  $Archive = Join-Path $DepsDir "$BoostName.7z"
  $Uri = "https://archives.boost.io/release/$BoostVersion/source/$BoostName.7z"
  Download-File $Uri $Archive
  Expand-With7z $Archive $DepsDir
  return $BoostRoot
}

function Ensure-RimePrebuilt {
  $Required = @(
    (Join-Path $WeaselRoot "include\rime_api.h"),
    (Join-Path $WeaselRoot "lib\rime.lib"),
    (Join-Path $WeaselRoot "lib64\rime.lib"),
    (Join-Path $OutputDir "rime.dll"),
    (Join-Path $OutputDir "Win32\rime.dll"),
    (Join-Path $OutputDir "data\opencc")
  )
  $Missing = $Required | Where-Object { !(Test-Path $_) }
  if (!$Missing) {
    return
  }
  if ($NoDownloads) {
    throw "Prebuilt librime files are missing and -NoDownloads was specified."
  }

  Write-Step "Downloading prebuilt librime"
  Push-Location $WeaselRoot
  try {
    $env:PATH = "$OutputDir;$env:PATH"
    $Args = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ".\get-rime.ps1", "-use", "dev")
    if ($RimeTag) {
      $Args += @("-tag", $RimeTag)
    }
    & powershell.exe @Args
    if ($LASTEXITCODE -ne 0) {
      throw "get-rime.ps1 failed"
    }
  }
  finally {
    Pop-Location
  }
}

function Ensure-QiwoData {
  $FrostRoot = Join-Path $RepoRoot "qiwo-ibusr/rime-frost"
  if (!(Test-Path (Join-Path $FrostRoot "essay.txt"))) {
    throw "rime-frost was not found at $FrostRoot"
  }

  Write-Step "Staging Qiwo Rime data"
  $DataDir = Join-Path $OutputDir "data"
  New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
  Copy-Item (Join-Path $FrostRoot "essay.txt") $DataDir -Force
  Copy-Item (Join-Path $FrostRoot "*.yaml") $DataDir -Force
  Copy-Item (Join-Path $WeaselRoot "LICENSE.txt") (Join-Path $OutputDir "LICENSE.txt") -Force
  Copy-Item (Join-Path $WeaselRoot "README.md") (Join-Path $OutputDir "README.txt") -Force

  $RimeInstall = Join-Path $OutputDir "rime-install.bat"
  if (!(Test-Path $RimeInstall)) {
    Set-Content -Path $RimeInstall -Encoding ASCII -Value @(
      "@echo off",
      "echo Qiwo bundles rime-frost by default.",
      "echo Additional schema installation is not bundled in this build."
    )
  }
}

function Ensure-Nsis {
  if ($NoInstaller) {
    return ""
  }

  $Candidates = @(
    (Join-Path ${env:ProgramFiles(x86)} "NSIS\Bin\makensis.exe"),
    (Join-Path ${env:ProgramFiles} "NSIS\Bin\makensis.exe"),
    (Join-Path $DepsDir "nsis\nsis-$NsisVersion\makensis.exe")
  )
  foreach ($Candidate in $Candidates) {
    if (Test-Path $Candidate) {
      return $Candidate
    }
  }
  if ($NoDownloads) {
    throw "makensis.exe is missing and -NoDownloads was specified."
  }

  Write-Step "Downloading portable NSIS $NsisVersion"
  $Archive = Join-Path $DepsDir "nsis-$NsisVersion.zip"
  $Uri = "https://sourceforge.net/projects/nsis/files/NSIS%203/$NsisVersion/nsis-$NsisVersion.zip/download"
  Download-File $Uri $Archive
  $NsisRoot = Join-Path $DepsDir "nsis"
  Expand-With7z $Archive $NsisRoot
  $Makensis = Join-Path $NsisRoot "nsis-$NsisVersion\makensis.exe"
  if (!(Test-Path $Makensis)) {
    throw "makensis.exe not found after extracting NSIS."
  }
  return $Makensis
}

function Test-BoostBuilt([string]$BoostRoot) {
  $StageLib = Join-Path $BoostRoot "stage\lib"
  if (!(Test-Path $StageLib)) {
    return $false
  }
  $ThreadLib = Get-ChildItem $StageLib -Filter "libboost_thread-*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
  $SystemLib = Get-ChildItem $StageLib -Filter "libboost_system-*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
  return [bool]($ThreadLib -and $SystemLib)
}

function Invoke-QiwoBuild([string]$VsDevCmd, [string]$BoostRoot, [string]$Makensis, [bool]$NeedBoostBuild) {
  Write-Step "Building Qiwo Weasel"
  $CmdFile = Join-Path ([IO.Path]::GetTempPath()) ("qiwo-weasel-build-" + [Guid]::NewGuid().ToString("N") + ".cmd")
  $BuildTarget = if ($NoInstaller) { "weasel" } else { "weasel installer" }
  if ($NeedBoostBuild) {
    $BuildTarget = "boost $BuildTarget"
  }
  Set-Content -Path (Join-Path $WeaselRoot "env.bat") -Encoding ASCII -Value @(
    "set WEASEL_ROOT=$WeaselRoot",
    "set BOOST_ROOT=$BoostRoot",
    "set BJAM_TOOLSET=msvc",
    "set PLATFORM_TOOLSET=v143"
  )
  $Lines = @(
    "@echo off",
    "call `"$VsDevCmd`" -arch=amd64 -host_arch=amd64",
    "if errorlevel 1 exit /b 1",
    "cd /d `"$WeaselRoot`"",
    "set BOOST_ROOT=$BoostRoot",
    "set BJAM_TOOLSET=msvc",
    "set PLATFORM_TOOLSET=v143",
    "set QIWO_FROST_ROOT=$RepoRoot\qiwo-ibusr\rime-frost"
  )
  if ($Makensis) {
    $Lines += "set MAKENSIS=$Makensis"
  }
  if ($NeedBoostBuild) {
    $Lines += "cd /d `"$BoostRoot`""
    $Lines += "if exist bin.v2 rmdir /s /q bin.v2"
    $Lines += "if not exist b2.exe call bootstrap.bat"
    $Lines += "if errorlevel 1 exit /b 1"
    $Lines += "set `"QIWO_VCVARS=%VCINSTALLDIR%Auxiliary\Build\vcvarsall.bat`""
    $Lines += "set `"QIWO_VCVARS=%QIWO_VCVARS:\=/%`""
    $Lines += "> project-config.jam echo import option ;"
    $Lines += ">> project-config.jam echo using msvc : 14.3 : `"%QIWO_VCVARS%`" ;"
    $Lines += ">> project-config.jam echo option.set keep-going : false ;"
    $Lines += "cd /d `"$WeaselRoot`""
  }
  $Lines += "call build.bat $BuildTarget"
  $Lines += "exit /b %ERRORLEVEL%"

  Set-Content -Path $CmdFile -Value $Lines -Encoding ASCII
  try {
    & cmd.exe /d /s /c "`"$CmdFile`""
    if ($LASTEXITCODE -ne 0) {
      throw "build.bat failed with exit code $LASTEXITCODE"
    }
  }
  finally {
    Remove-Item $CmdFile -Force -ErrorAction SilentlyContinue
  }
}

Write-Step "Checking toolchain"
$VsDevCmd = Get-VsDevCmd
$BoostRoot = Ensure-Boost
Ensure-RimePrebuilt
Ensure-QiwoData
$Makensis = Ensure-Nsis
$NeedBoostBuild = !(Test-BoostBuilt $BoostRoot)
Invoke-QiwoBuild $VsDevCmd $BoostRoot $Makensis $NeedBoostBuild

Write-Host ""
Write-Host "Qiwo Weasel build completed."
if (!$NoInstaller) {
  Get-ChildItem (Join-Path $OutputDir "archives") -Filter "*.exe" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 3 FullName, Length, LastWriteTime |
    Format-Table -AutoSize
}
