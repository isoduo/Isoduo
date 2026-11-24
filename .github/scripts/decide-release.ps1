# Release workflow step: fetch what Get-IsoduoReleaseDecision needs from GitHub, decide, and pass the result on.
# Writes the tag and title as step outputs, and the draft's reminder notes to -NotesPath when given.
param([string]$NotesPath)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/ReleaseDecision.ps1"

$repository = $env:GITHUB_REPOSITORY
$commit = $env:GITHUB_SHA
function Get-GitHubLines([string]$Path, [string]$Filter) {
    $lines = gh api --paginate $Path --jq $Filter
    if ($LASTEXITCODE -ne 0) { throw "GitHub API request failed: $Path" }
    @($lines | Where-Object { $_ })
}
$tags = Get-GitHubLines "repos/$repository/tags?per_page=100" '.[].name'
# GitHub lists draft releases only to tokens that can write contents.
$releases = Get-GitHubLines "repos/$repository/releases?per_page=100" '.[] | {TagName: .tag_name, Draft: .draft}' |
    ForEach-Object { $_ | ConvertFrom-Json }
$runs = Get-GitHubLines "repos/$repository/actions/runs?head_sha=$commit&event=push&branch=main&per_page=100" `
    '.workflow_runs[] | {Name: .name, Status: .status, Conclusion: .conclusion, CreatedAt: .created_at}' |
    ForEach-Object { $_ | ConvertFrom-Json }

$decision = Get-IsoduoReleaseDecision -Ref $env:GITHUB_REF -Commit $commit -Repository $repository `
    -CMakeLists (Get-Content -Raw -LiteralPath "$PSScriptRoot/../../CMakeLists.txt") -Tags $tags -Releases $releases -Runs $runs
if ($decision.Failure) {
    Write-Host "::error::$($decision.Failure)"
    exit 1
}
Write-Host "$($decision.Title) can be drafted as $($decision.Tag) at $commit."
if ($env:GITHUB_OUTPUT) { "tag=$($decision.Tag)", "title=$($decision.Title)" | Out-File -Append -Encoding utf8 -LiteralPath $env:GITHUB_OUTPUT }
if ($NotesPath) { Set-Content -Encoding utf8 -LiteralPath $NotesPath -Value $decision.Notes }
