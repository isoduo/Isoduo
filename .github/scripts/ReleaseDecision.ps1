# Decides whether a release can be drafted, from data the release workflow has already fetched from GitHub.
# Returns Tag, Title and Notes when it can; otherwise Failure holds one sentence saying why not.
function Get-IsoduoReleaseDecision {
    param(
        [string]$Ref,
        [string]$CMakeLists,
        [string]$Commit,
        [string]$Repository,
        [string[]]$Tags = @(),
        [object[]]$Releases = @(),
        [object[]]$Runs = @()
    )
    function Refuse([string]$Message) { [pscustomobject]@{ Tag = $null; Title = $null; Notes = $null; Failure = $Message } }
    if ($Ref -cne 'refs/heads/main') { return Refuse "Releases are made only from main; this run started from $Ref." }
    # CMake command names ignore case; the project name and the VERSION keyword do not.
    $version = [regex]::Match($CMakeLists, '(?m)^\s*(?i:project)\s*\(\s*Isoduo\s+VERSION\s+([^\s)]+)').Groups[1].Value
    if ($version -notmatch '^\d+\.\d+\.\d+$') {
        return Refuse "project(Isoduo VERSION ...) in CMakeLists.txt must be three numbers such as 0.4.0; found '$version'."
    }
    $tag = "v$version"
    $releasesForTag = @($Releases | Where-Object { $_.TagName -ceq $tag })
    if (@($releasesForTag | Where-Object { -not $_.Draft }).Count) {
        return Refuse "Release $tag is already published; bump project(Isoduo VERSION ...) in CMakeLists.txt first."
    }
    if ($Tags -ccontains $tag) {
        return Refuse "The tag $tag already exists; bump project(Isoduo VERSION ...) in CMakeLists.txt first."
    }
    if ($releasesForTag.Count) {
        return Refuse "A draft release for $tag already exists; delete it on the Releases page and run the workflow again."
    }
    # UIA trial is still on trial and does not gate releases. A re-run reuses its run; separate runs are compared by age.
    $problems = @(foreach ($name in @('Core', 'Render')) {
        $latest = @($Runs | Where-Object { $_.Name -ceq $name } | Sort-Object CreatedAt -Descending) | Select-Object -First 1
        if (-not $latest) { "$name has no run" }
        elseif ($latest.Status -cne 'completed') { "$name is $($latest.Status)" }
        elseif ($latest.Conclusion -cne 'success') { "$name concluded $($latest.Conclusion)" }
    })
    if ($problems.Count) { return Refuse "Core and Render must succeed on $Commit first: $($problems -join '; ')." }
    $readme = "https://github.com/$Repository/blob/$Commit/README.md#real-platform-runs-per-change"
    $notes = @(
        "Commit: ``$Commit``",
        '',
        "Before publishing, check out this commit on the author machine and do the real-platform runs this release needs. The [Real-platform runs per change]($readme) table lists the idle-desktop runs each change requires, and ``verify-v1-final.ps1`` runs the full real-platform acceptance pass. Which runs to do is the author's call; this workflow does not check them. Replace this paragraph with the release summary, including which real-platform runs you did or that CTest was enough."
    ) -join "`n"
    [pscustomobject]@{ Tag = $tag; Title = "Isoduo $version"; Notes = $notes; Failure = $null }
}
