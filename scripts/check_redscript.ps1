<#
Compile every redscript mod in the game's r6\scripts the way RED4ext does at launch (plugin
script folders included), writing to a scratch cache so the game's own cache is untouched.
Run this after changing any .reds file and BEFORE launching: one error disables every
redscript mod at once.

Usage: pwsh scripts\check_redscript.ps1 -GameRoot "<game root>"
Exit code 0 = clean; otherwise the errors are printed from redscript_rCURRENT.log.
#>
param(
    [string]$GameRoot = "C:\Games\Steam\steamapps\common\Cyberpunk 2077",
    [string]$Scratch = (Join-Path $env:TEMP "cpvr-redscript-check")
)
$ErrorActionPreference = "Stop"
$scc = Join-Path $GameRoot "engine\tools\scc.exe"
if (-not (Test-Path $scc)) { throw "redscript compiler not found: $scc" }
New-Item -ItemType Directory -Force $Scratch | Out-Null
$paths = Get-ChildItem (Join-Path $GameRoot "red4ext\plugins\*\Scripts") -Directory | ForEach-Object FullName
$pathsFile = Join-Path $Scratch "paths.txt"
$paths | Set-Content $pathsFile
$log = Join-Path $GameRoot "r6\logs\redscript_rCURRENT.log"
& $scc -compile (Join-Path $GameRoot "r6\scripts") -compilePathsFile $pathsFile -outputCacheFile (Join-Path $Scratch "final.redscripts") (Join-Path $GameRoot "r6\cache\final.redscripts") 2>&1 | Out-Null
$code = $LASTEXITCODE
if ($code -eq 0) {
    Write-Host "[ok] redscript compiles clean ($($paths.Count) plugin script folders included)"
} else {
    Write-Host "[!!] redscript compilation FAILED:"
    Get-Content $log | Select-String '\[ERROR' -Context 0,4 | ForEach-Object { $_.Line; $_.Context.PostContext | ForEach-Object { "    $_" } }
}
exit $code
