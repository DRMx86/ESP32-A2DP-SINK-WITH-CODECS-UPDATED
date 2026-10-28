param([switch]$WithLhdc,[switch]$WithGatt)
$ErrorActionPreference = 'Stop'
$nativeRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent $nativeRoot
$idfRoot = Join-Path $workspaceRoot 'esp-idf'
$buildRoot = Join-Path $nativeRoot '_build\esp32'
$probeRoot = Join-Path $nativeRoot 'esp32_probe'
$env:GIT_NO_LAZY_FETCH = '1'
if (-not (Test-Path -LiteralPath (Join-Path $idfRoot '.git'))) {
    # The bundled SDK is not this host repository's Git checkout.
    $env:GIT_CEILING_DIRECTORIES = $workspaceRoot
}

python (Join-Path $PSScriptRoot 'check_workspace.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
. (Join-Path $idfRoot 'export.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$env:IDF_COMPONENT_MANAGER = '0'
$env:CCACHE_DISABLE = '1'
$lhdcBuild = if ($WithLhdc) { 'ON' } else { 'OFF' }
$gattBuild = if ($WithGatt) { 'ON' } else { 'OFF' }
python (Join-Path $idfRoot 'tools\idf.py') -C $probeRoot -B $buildRoot "-DDBT_PROBE_LHDC_BUILD=$lhdcBuild" "-DDBT_PROBE_GATT_TEST=$gattBuild" build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
python (Join-Path $PSScriptRoot 'check_workspace.py')
exit $LASTEXITCODE
