<#
.SYNOPSIS
    Reads, bumps or sets the one version wifiForce carries.

.DESCRIPTION
    version.txt is the only place the version lives.  tools\build-files.ps1
    writes it into the resource block (file properties), the window title and
    the executable's name, so editing that one line keeps the tag, the file
    properties, the build's name and what the application displays in
    agreement.

    The version has two numbers.  Three are accepted as well, so a project that
    already carries a patch number keeps working, but wifiForce does not use
    one: a build that carries a change bumps the minor number.

    This script is that edit, and build.bat calls it before it compiles.  It
    prints the resulting version on stdout and nothing else, so the caller can
    capture it.

.PARAMETER KeepVersion
    Keep the current version: rebuild what is there without a new number.

.PARAMETER Version
    Set the version outright, for a deliberate bump, e.g. -Version 2.0.
    Nothing is incremented on top of it.

.EXAMPLE
    .\tools\version.ps1
    1.1

.EXAMPLE
    .\tools\version.ps1 -KeepVersion
    1.1
#>
[CmdletBinding()]
param(
    [switch] $KeepVersion,
    [string] $Version
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$file = Join-Path $root 'version.txt'
if (-not (Test-Path -LiteralPath $file)) {
    Write-Error "version.txt was not found at $file"
    exit 1
}

$current = ([System.IO.File]::ReadAllText($file)).Trim()
if ($current -notmatch '^(\d+)\.(\d+)(\.(\d+))?$') {
    Write-Error "version.txt holds '$current', which is not a version like 1.2"
    exit 1
}
$major = [int] $Matches[1]
$minor = [int] $Matches[2]
$hasPatch = ($current.Split('.').Count -eq 3)
$patch = if ($hasPatch) { [int] $current.Split('.')[2] } else { 0 }

$next = $current

if ($Version) {
    if ($KeepVersion) {
        Write-Error 'Use either -Version or -KeepVersion, not both'
        exit 1
    }
    if ($Version -notmatch '^\d+\.\d+(\.\d+)?$') {
        Write-Error "-Version must be two numbers like 1.2, not '$Version'"
        exit 1
    }
    $next = $Version
} elseif (-not $KeepVersion) {
    # A build that carries a change bumps the last number: the leading one is a
    # deliberate decision, made with -Version.
    $next = if ($hasPatch) { '{0}.{1}.{2}' -f $major, $minor, ([int] $patch + 1) }
            else          { '{0}.{1}' -f $major, ([int] $minor + 1) }
}

if ($next -ne $current) {
    [System.IO.File]::WriteAllText($file, $next + "`n", (New-Object System.Text.UTF8Encoding($false)))
}

Write-Output $next
