<#
.SYNOPSIS
    Build the HybridXRay Engine.sln from a clean PowerShell session.

.PARAMETER Configuration
    Release (default), Debug, or ReleaseNoEditor.

.PARAMETER Platform
    x64 (default; the only supported platform).

.PARAMETER Target
    MSBuild target. Defaults to the empty string (i.e. Build).
    Use 'Rebuild' or 'Clean' for full passes.

.PARAMETER Verbosity
    MSBuild verbosity (quiet|minimal|normal|detailed|diagnostic). Default: minimal.

.PARAMETER WindowsSdk
    Override WindowsTargetPlatformVersion. Default: 10.0.19041.0
    (the registry advertises 10.0.26100 on some machines but only 19041 is on disk).

.PARAMETER VCToolsVersion
    Pin the MSVC toolset. Default: 14.35.32215. Newer toolsets (14.44)
    drop an inline template instantiation in xrCore under /GL.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Configuration Debug
    .\build.ps1 -Target Rebuild -Verbosity normal
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'ReleaseNoEditor')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [string]$Target = '',

    [ValidateSet('quiet', 'minimal', 'normal', 'detailed', 'diagnostic')]
    [string]$Verbosity = 'minimal',

    [string]$WindowsSdk = '10.0.19041.0',

    [string]$VCToolsVersion = '14.35.32215',

    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'

$root    = $PSScriptRoot
$sln     = Join-Path $root 'Source\Engine.sln'
$logDir  = Join-Path $root '_BuildLogs'
$logFile = Join-Path $logDir ("build_{0}_{1}.log" -f $Configuration, $Platform)

if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }
if (-not (Test-Path $sln))    { throw "Solution not found: $sln" }

# Locate MSBuild via vswhere.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found at $vswhere. Install Visual Studio 2022." }

$vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) { throw "No Visual Studio installation with MSBuild found." }

$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw "MSBuild not found at $msbuild" }

$msbuildArgs = @(
    $sln,
    '/m',
    '/nologo',
    "/v:$Verbosity",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:WindowsTargetPlatformVersion=$WindowsSdk",
    "/p:VCToolsVersion=$VCToolsVersion",
    '/fl',
    "/flp:logfile=$logFile;verbosity=normal"
)
if ($Target) { $msbuildArgs += "/t:$Target" }

Write-Host "MSBuild:       $msbuild"
Write-Host "Solution:      $sln"
$targetLabel = if ($Target) { $Target } else { 'Build' }
Write-Host "Configuration: $Configuration | Platform: $Platform | Target: $targetLabel"
Write-Host "SDK: $WindowsSdk | Toolset: $VCToolsVersion"
Write-Host "Log:           $logFile"
Write-Host ""

& $msbuild @msbuildArgs
$exit = $LASTEXITCODE

Write-Host ""
if ($exit -eq 0) {
    $binDir = Join-Path $root ("Bin\{0}\{1}" -f $Platform, $Configuration)
    Write-Host "Build OK. Binaries in: $binDir" -ForegroundColor Green
} else {
    Write-Host "Build FAILED (exit $exit). See log: $logFile" -ForegroundColor Red
}

if (-not $NoPause -and $Host.UI.RawUI -and $Host.Name -ne 'ServerRemoteHost') {
    Write-Host ""
    Write-Host "Press any key to exit..." -ForegroundColor Yellow
    [void]$Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
}

exit $exit
