[CmdletBinding()]
param(
    [ValidateSet('x64', 'Win32', 'ARM64')]
    [string]$Architecture = 'x64',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $root 'build'

cmake -S $root -B $buildDirectory -A $Architecture
cmake --build $buildDirectory --config $Configuration --parallel

$executable = Join-Path $buildDirectory "$Configuration\InternetSpeedMeter.exe"
if (Test-Path $executable) {
    Write-Host "Built: $executable"
} else {
    throw "Build completed but InternetSpeedMeter.exe was not found."
}
