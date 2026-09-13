param([string]$OpenOcdRoot = $env:OPENOCD_ROOT)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$image = (Join-Path $project 'build/ecg_freertos.hex').Replace('\','/')
if (!(Test-Path -LiteralPath $image)) { throw 'Build the firmware first.' }
& "$OpenOcdRoot/bin/openocd.exe" -s "$OpenOcdRoot/share/openocd/scripts" -f interface/stlink.cfg -f target/stm32f1x.cfg -c 'reset_config none' -c 'adapter speed 1000' -c "program {$image} verify reset exit"
if ($LASTEXITCODE -ne 0) { throw 'ST-Link download or verification failed.' }
