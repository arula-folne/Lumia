param(
  [Parameter(Mandatory = $true)][string]$DllPath,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [Parameter(Mandatory = $true)][string]$Name
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$exports = & dumpbin /exports $DllPath | Out-String
$lines = $exports -split "`r?`n"
$funcs = New-Object System.Collections.Generic.List[string]
$inTable = $false
foreach ($line in $lines) {
  if ($line -match '^\s*ordinal\s+hint\s+RVA\s+name') { $inTable = $true; continue }
  if ($inTable) {
    if ($line -match '^\s*Summary') { break }
    if ($line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') {
      $funcs.Add($Matches[1]) | Out-Null
    }
  }
}

if ($funcs.Count -lt 10) {
  throw "Failed to parse exports from $DllPath"
}

$defPath = Join-Path $OutDir "$Name.def"
$libPath = Join-Path $OutDir "$Name.lib"
$linesOut = @("LIBRARY $Name", "EXPORTS") + ($funcs | ForEach-Object { "  $_" })
Set-Content -Path $defPath -Value $linesOut -Encoding ASCII

& lib "/def:$defPath" "/out:$libPath" "/machine:x64" | Out-Null
if (-not (Test-Path $libPath)) {
  throw "lib.exe failed to create $libPath"
}

Write-Output "Created $libPath ($($funcs.Count) exports)"
