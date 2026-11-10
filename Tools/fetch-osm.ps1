# Downloads the OpenStreetMap data for one city district from the Overpass API and writes
#   Tools\Data\osm\<Name>.json        raw Overpass JSON (out body geom)
#   Tools\Data\osm\<Name>.meta.json   bbox, query, endpoint and fetch date
# then runs Tools\Editor\parse_osm.py once to produce <Name>.buildings.json.
#
# Plain HTTP, no engine. The data is ODbL: credit "(c) OpenStreetMap contributors".
#
#   .\Tools\fetch-osm.ps1                 # the East Village block, skips the download if present
#   .\Tools\fetch-osm.ps1 -Force          # download again
#
# Bounding box order is Overpass's: south, west, north, east (degrees WGS84).

[CmdletBinding()]
param(
    [string]$Name = "east_village",
    [double]$South = 40.7231,
    [double]$West = -73.9866,
    [double]$North = 40.7297,
    [double]$East = -73.9767,
    [switch]$Force,
    [string]$Engine = ""
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OutDir = Join-Path $ScriptDir "Data\osm"
$RawFile = Join-Path $OutDir "$Name.json"
$MetaFile = Join-Path $OutDir "$Name.meta.json"
$Parser = Join-Path $ScriptDir "Editor\parse_osm.py"
$Python = Join-Path $Engine "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$inv = [System.Globalization.CultureInfo]::InvariantCulture
$bbox = [string]::Format($inv, "{0},{1},{2},{3}", $South, $West, $North, $East)

# Buildings of every kind (not only ones with a height tag), roads, parks, and POIs for later.
$query = @"
[out:json][timeout:90];
(
  way["building"]($bbox);
  relation["building"]($bbox);
  way["highway"]($bbox);
  way["leisure"="park"]($bbox);
  relation["leisure"="park"]($bbox);
  node["amenity"]($bbox);
  node["shop"]($bbox);
);
out body geom;
"@

$endpoints = @(
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter"
)

if ((Test-Path $RawFile) -and -not $Force) {
    Write-Host "Already have $RawFile (use -Force to download again)."
} else {
    $used = $null
    foreach ($url in $endpoints) {
        try {
            Write-Host "POST $url  bbox=$bbox"
            $body = "data=" + [System.Uri]::EscapeDataString($query)
            # overpass-api.de answers 406 to the default PowerShell user agent.
            $headers = @{ "Accept" = "application/json" }
            $resp = Invoke-WebRequest -Uri $url -Method Post -Body $body -Headers $headers `
                -UserAgent "hawkeye-city-generator/1.0 (hobby game project; github.com/cameronjim/hawkeye)" `
                -ContentType "application/x-www-form-urlencoded" -UseBasicParsing -TimeoutSec 180
            if ($resp.StatusCode -ne 200) { throw "HTTP $($resp.StatusCode)" }
            $text = $resp.Content
            if ($text -is [byte[]]) { $text = [System.Text.Encoding]::UTF8.GetString($text) }
            if (-not $text.TrimStart().StartsWith("{")) { throw "response is not JSON" }
            [System.IO.File]::WriteAllText($RawFile, $text, (New-Object System.Text.UTF8Encoding $false))
            $used = $url
            break
        } catch {
            Write-Warning "Overpass request to $url failed: $_"
        }
    }
    if (-not $used) { throw "Every Overpass endpoint failed." }

    $meta = [ordered]@{
        name        = $Name
        bbox        = [ordered]@{ south = $South; west = $West; north = $North; east = $East }
        endpoint    = $used
        fetched_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
        licence     = "ODbL 1.0, (c) OpenStreetMap contributors"
        query       = $query
    }
    $metaJson = $meta | ConvertTo-Json -Depth 5
    [System.IO.File]::WriteAllText($MetaFile, $metaJson, (New-Object System.Text.UTF8Encoding $false))
    Write-Host "Wrote $RawFile and $MetaFile"
}

if (-not (Test-Path $Python)) { throw "Engine Python not found: $Python" }
& $Python $Parser $RawFile
exit $LASTEXITCODE
