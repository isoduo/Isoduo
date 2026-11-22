$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../.github/scripts/ReleaseDecision.ps1"

$commit = '0123456789abcdef0123456789abcdef01234567'
$cmakeLists = "cmake_minimum_required(VERSION 3.25)`nproject(Isoduo VERSION 0.4.0 LANGUAGES C CXX)`n"
function New-Run([string]$Name, [string]$Status = 'completed', [string]$Conclusion = 'success', [string]$CreatedAt = '2026-11-22T10:00:00Z') {
    [pscustomobject]@{ Name = $Name; Status = $Status; Conclusion = $Conclusion; CreatedAt = $CreatedAt }
}
function Invoke-Decision([hashtable]$Change = @{}) {
    $arguments = @{
        Ref = 'refs/heads/main'; CMakeLists = $cmakeLists; Commit = $commit; Repository = 'isoduo/Isoduo'
        Tags = @(); Releases = @(); Runs = @((New-Run 'Core'), (New-Run 'Render'), (New-Run 'UIA trial'))
    }
    foreach ($key in $Change.Keys) { $arguments[$key] = $Change[$key] }
    Get-IsoduoReleaseDecision @arguments
}

$release = Invoke-Decision
if ($release.Failure) { throw "Green main was refused: $($release.Failure)" }
if ($release.Tag -cne 'v0.4.0') { throw "Wrong tag: $($release.Tag)" }
if ($release.Title -cne 'Isoduo 0.4.0') { throw "Wrong title: $($release.Title)" }
foreach ($expected in @(
    $commit,
    "https://github.com/isoduo/Isoduo/blob/$commit/README.md#real-platform-runs-per-change",
    'verify-v1-final.ps1'
)) {
    if (-not $release.Notes.Contains($expected)) { throw "Notes lack '$expected':`n$($release.Notes)" }
}

function Assert-Refused([string]$Case, [hashtable]$Change, [string]$Pattern) {
    $decision = Invoke-Decision $Change
    if (-not $decision.Failure) { throw "$Case was accepted as $($decision.Tag)" }
    if ($decision.Tag -or $decision.Title -or $decision.Notes) { throw "$Case was refused but still named a release" }
    if ($decision.Failure -notlike $Pattern) { throw "$Case gave '$($decision.Failure)', expected '$Pattern'" }
}

Assert-Refused 'feature branch' @{ Ref = 'refs/heads/feature/release' } '*main*refs/heads/feature/release*'
Assert-Refused 'tag ref named main' @{ Ref = 'refs/tags/main' } '*main*refs/tags/main*'

$wrapped = Invoke-Decision @{ CMakeLists = "PROJECT(Isoduo`n  VERSION 1.12.3`n  LANGUAGES C CXX)`n" }
if ($wrapped.Tag -cne 'v1.12.3' -or $wrapped.Title -cne 'Isoduo 1.12.3') { throw "Wrapped project() gave $($wrapped.Tag) / $($wrapped.Title) / $($wrapped.Failure)" }
foreach ($version in @('0.4', '0.4.0.1', '0.4.0-rc1')) {
    Assert-Refused "version $version" @{ CMakeLists = "project(Isoduo VERSION $version LANGUAGES C CXX)" } "*three numbers*'$version'*"
}
Assert-Refused 'no version' @{ CMakeLists = 'project(Isoduo LANGUAGES C CXX)' } "*three numbers*''*"

function New-Release([string]$TagName, [bool]$Draft) { [pscustomobject]@{ TagName = $TagName; Draft = $Draft } }
$others = Invoke-Decision @{
    Tags = @('v0.3.0', 'v0.4.0-rc1', 'v0.4.01')
    Releases = @((New-Release 'v0.3.0' $false), (New-Release 'v0.4.0-rc1' $true), (New-Release 'v0.5.0' $true))
}
if ($others.Failure -or $others.Tag -cne 'v0.4.0') { throw "Other versions blocked v0.4.0: $($others.Failure)" }
Assert-Refused 'tag only' @{ Tags = @('v0.4.0') } '*tag v0.4.0 already exists*bump*'
Assert-Refused 'published' @{ Tags = @('v0.4.0'); Releases = @(New-Release 'v0.4.0' $false) } '*v0.4.0 is already published*bump*'
Assert-Refused 'draft' @{ Releases = @(New-Release 'v0.4.0' $true) } '*draft*v0.4.0*delete*'
Assert-Refused 'draft for an existing tag' @{ Tags = @('v0.4.0'); Releases = @(New-Release 'v0.4.0' $true) } '*tag v0.4.0 already exists*bump*'

$trialFailed = Invoke-Decision @{ Runs = @((New-Run 'Core'), (New-Run 'Render'), (New-Run 'UIA trial' 'completed' 'failure')) }
if ($trialFailed.Failure) { throw "UIA trial blocked the release: $($trialFailed.Failure)" }
Assert-Refused 'Core failed' @{ Runs = @((New-Run 'Core' 'completed' 'failure'), (New-Run 'Render')) } "*$commit*Core concluded failure*"
Assert-Refused 'Core cancelled' @{ Runs = @((New-Run 'Core' 'completed' 'cancelled'), (New-Run 'Render')) } '*Core concluded cancelled*'
Assert-Refused 'Render running' @{ Runs = @((New-Run 'Core'), (New-Run 'Render' 'in_progress' $null)) } '*Render is in_progress*'
Assert-Refused 'Render missing' @{ Runs = @((New-Run 'Core'), (New-Run 'UIA trial')) } '*Render has no run*'
$both = Invoke-Decision @{ Runs = @((New-Run 'Core' 'completed' 'failure'), (New-Run 'Render' 'queued' $null)) }
if ($both.Failure -notlike '*Core concluded failure*Render is queued*') { throw "Both checks were not named: $($both.Failure)" }
$retried = Invoke-Decision @{ Runs = @(
    (New-Run 'Core' 'completed' 'success' '2026-11-22T11:00:00Z'), (New-Run 'Core' 'completed' 'failure' '2026-11-22T10:00:00Z'), (New-Run 'Render')
) }
if ($retried.Failure) { throw "An older failed Core run outweighed a newer success: $($retried.Failure)" }
Assert-Refused 'newer Core failure' @{ Runs = @(
    (New-Run 'Core' 'completed' 'success' '2026-11-22T10:00:00Z'), (New-Run 'Core' 'completed' 'failure' '2026-11-22T11:00:00Z'), (New-Run 'Render')
) } '*Core concluded failure*'

Write-Host 'Release decision passed'
