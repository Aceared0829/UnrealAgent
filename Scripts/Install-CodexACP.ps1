param(
    [string]$Version = "1.11.0",
    [Parameter(Mandatory = $true)]
    [string]$ProjectRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if (-not (Get-ChildItem -LiteralPath $ProjectRoot -Filter *.uproject -File -ErrorAction Stop))
{
    throw "ProjectRoot must contain a .uproject file."
}
# Reuse the editor's integrity-checked installer instead of resolving a floating dependency tree.
$npm = Get-Command npm.cmd -ErrorAction Stop
& (Join-Path $PSScriptRoot "Install-AgentCli.ps1") -Provider Codex -ProjectSavedDirectory (Join-Path $ProjectRoot "Saved") -NpmPath $npm.Source -CodexAcpVersion $Version
