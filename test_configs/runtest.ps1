# Runs one MaxCraft render-crash A/B test from test_configs/ and reports the result.
#
#   .\runtest.ps1 test_20_groupA.ini [-WaitSeconds 75] [-NoOverlay]
#
# Steps, none of them skippable:
#   1. refuses to run if the game is already up (a locked .asi and a muddled result)
#   2. copies the built MaxCraft.asi into the game folder and verifies the hash
#   3. merges the test's [Debug] keys into the game's MaxCraft.ini - ADDING keys that are missing,
#      which a plain search-and-replace does not do and which silently voided one run already
#   4. archives MaxCraft.log and starts a fresh one
#   5. launches, waits, and classifies what happened
#   6. retries if the crash it caught is the one that is not ours
#
# It never guesses. "NO CRASH" means no crash line, no WER event, and the game still running; anything
# else is reported as what it actually is.

param(
    [Parameter(Mandatory = $true)][string]$Test,
    [int]$WaitSeconds = 75,
    [switch]$NoOverlay,
    [int]$MaxAttempts = 4,
    [string]$AsiPath = 'C:\Users\Public\mcbuild\MaxCraft.asi'
)

$ErrorActionPreference = 'Stop'

$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Max Payne 2 The Fall of Max Payne'
$Repo    = Split-Path -Parent $PSScriptRoot
$Configs = Join-Path $Repo 'test_configs'
$Backup  = Join-Path $env:TEMP 'opencode\maxcraft-backup'
$Log     = Join-Path $GameDir 'MaxCraft.log'
$Ini     = Join-Path $GameDir 'MaxCraft.ini'

if (Get-Process maxpayne2 -ErrorAction SilentlyContinue) {
    Write-Error 'the game is already running - close it first, or the .asi cannot be replaced and the result is mixed'
}
if (-not (Test-Path $AsiPath)) { Write-Error "build not found: $AsiPath" }
$testFile = Join-Path $Configs $Test
if (-not (Test-Path $testFile)) { Write-Error "no such test config: $testFile" }

New-Item -ItemType Directory -Force -Path $Backup | Out-Null

# --- deploy -----------------------------------------------------------------------------------
Copy-Item $AsiPath (Join-Path $GameDir 'MaxCraft.asi') -Force
if ((Get-FileHash $AsiPath -Algorithm SHA256).Hash -ne (Get-FileHash (Join-Path $GameDir 'MaxCraft.asi') -Algorithm SHA256).Hash) {
    Write-Error 'the .asi in the game folder is not the one that was built'
}
$asiBytes = (Get-Item (Join-Path $GameDir 'MaxCraft.asi')).Length

# --- config: merge, never replace --------------------------------------------------------------
# A key the test file sets but the game's ini has never heard of is INSERTED. That step is the whole
# reason this is a merge: MaxCraft.ini in the game folder predates the newer switches, and a run that
# asks for bNoVtableSelfCheck = 1 in a file with no such line silently gets 0.
$wanted   = [ordered]@{}
$inDebug  = $false
foreach ($line in Get-Content $testFile) {
    $t = $line.Trim()
    if ($t -match '^\[(.+)\]$') { $inDebug = ($Matches[1] -eq 'Debug'); continue }
    if (-not $inDebug -or $t -eq '' -or $t.StartsWith(';')) { continue }
    if ($t -match '^([^=]+?)\s*=\s*(.*)$') { $wanted[$Matches[1].Trim()] = $Matches[2].Trim() }
}

Copy-Item $Ini (Join-Path $Backup "MaxCraft.ini.$Test.once") -Force
$raw = Get-Content $Ini -Raw
foreach ($k in $wanted.Keys) {
    $line = "$k = $($wanted[$k])"
    if ($raw -match "(?m)^$([regex]::Escape($k))\s*=.*$") {
        $raw = $raw -replace "(?m)^$([regex]::Escape($k))\s*=.*$", $line
    } elseif ($raw -match '(?m)^\[Debug\]\s*$') {
        $raw = $raw -replace '(?m)^\[Debug\]\s*$', "[Debug]`n$line"
    } else {
        $raw = $raw.TrimEnd() + "`n`n[Debug]`n$line`n"
    }
}
Set-Content -Path $Ini -Value $raw -Encoding UTF8 -NoNewline

Write-Host "TEST $Test   (wait ${WaitSeconds}s, up to $MaxAttempts attempts)"
Write-Host "  asi    : $asiBytes bytes"
Write-Host "  config : $($wanted.Keys -join ', ')"

# --- one attempt -------------------------------------------------------------------------------
function Invoke-Attempt {
    param([int]$Wait)

    if (Test-Path $Log) {
        Copy-Item $Log (Join-Path $Backup "MaxCraft.log.$Test.$(Get-Date -Format HHmmss)") -Force
        Remove-Item $Log -Force
    }

    $startedAt = Get-Date
    $uri = if ($NoOverlay) { 'steam://run/12150//-nooverlay' } else { 'steam://rungameid/12150' }
    Start-Process $uri
    Start-Sleep -Seconds $Wait

    $alive = [bool](Get-Process maxpayne2 -ErrorAction SilentlyContinue)
    $crashLine = $null
    $hookLine = $null
    if (Test-Path $Log) {
        $crashLine = Select-String -Path $Log -Pattern '\] CRASH 0x' -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $crashLine) { $hookLine = Select-String -Path $Log -Pattern 'LAST HOOK:' -ErrorAction SilentlyContinue | Select-Object -Last 1 }
    }
    $wer = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $startedAt } -ErrorAction SilentlyContinue |
           Where-Object { $_.Message -match 'maxpayne2' } | Select-Object -First 1

    # Stop the game so the next attempt can replace the .asi.
    Get-Process maxpayne2 -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 5

    if (Test-Path $Log) {
        Write-Host ''
        Write-Host '  === the lines that decide it ==='
        Select-String -Path $Log -Pattern '\[BUILD\]|diagnostic:|device hooks:|LAST HOOK|EXCEPTION 0x|CRASH 0x|self-check|WVP ENTER' |
            ForEach-Object { '    ' + $_.Line.Trim() } | Select-Object -First 24
    }

    # Crash A is the one that is not ours: it happens with MaxCraft.asi removed from the game folder
    # entirely, in MP2's own window procedure, before any D3D8 device exists. The ring being empty is
    # how that is told apart from the crash we are isolating - the ring is filled by CreateDevice.
    $ringEmpty = $false
    if (Test-Path $Log) {
        $last = Select-String -Path $Log -Pattern 'LAST HOOK: NONE' -ErrorAction SilentlyContinue | Select-Object -First 1
        $ringEmpty = [bool]$last
    }

    if ($crashLine) {
        $where = if ($ringEmpty) { 'A' } else { 'B' }
        return "$where`: crash caught by our handler"
    }
    if ($wer) { return "A?: Windows logged a fault but our handler wrote nothing (fault $($wer.TimeCreated.ToString('HH:mm:ss'))" }
    if ($alive) { return 'NO CRASH so far - the game was still running when the wait ended' }
    return 'INCONCLUSIVE - the game was not running and nothing crashed; it was closed or never started'
}

# Steam's -nooverlay launch option does not take effect through a steam:// URL - the overlay is still
# on the stack - so crash A cannot be switched off from here and fires on its own schedule. Rather
# than report a run that measured it, retry until a run measures the thing under test.
$verdict = 'NO RUN'
for ($i = 1; $i -le $MaxAttempts; $i++) {
    $v = Invoke-Attempt -Wait $WaitSeconds
    if ($v -notlike 'A*') { $verdict = $v; break }
    Write-Host ''
    Write-Host "  attempt $i was crash A (Steam overlay, not the render path) - retrying"
    $verdict = $v
}

Write-Host ''
Write-Host '=== RESULT ==='
Write-Host "  $verdict"
if ($verdict -like 'A*') {
    Write-Host '  (crash A: maxpayne2.exe+0x1b80 in MP2'"'"'s window procedure, with the Steam overlay in the'
    Write-Host '   stack and no D3D8 device ever created. Reproduces with MaxCraft.asi removed from the game'
    Write-Host '   folder, so it is not the render path. Increase -MaxAttempts or disable the overlay in'
    Write-Host '   Steam to get a run that measures what this test is for.)'
}