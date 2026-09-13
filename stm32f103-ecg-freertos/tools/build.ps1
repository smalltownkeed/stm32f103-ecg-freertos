param(
    [string]$ArmBin = $env:ARM_GCC_BIN,
    [string]$NativeBin = $env:NATIVE_GCC_BIN,
    [switch]$FirmwareOnly
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$build = Join-Path $project 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:PATH = "$ArmBin;$NativeBin;" + $env:PATH
function Run-Tool([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE)" }
}
$kernel = "$project/third_party/FreeRTOS"
$flags = @('-mcpu=cortex-m3','-mthumb','-std=c11','-Os','-g3','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections','-fstack-usage','-Wall','-Wextra','-Werror',"-I$project/firmware","-I$project/common","-I$kernel/include","-I$kernel/portable/GCC/ARM_CM3")
$sources = @('firmware/startup.S','firmware/board.c','firmware/app.c','firmware/runtime.c','common/protocol.c','common/history.c','third_party/FreeRTOS/tasks.c','third_party/FreeRTOS/list.c','third_party/FreeRTOS/queue.c','third_party/FreeRTOS/portable/GCC/ARM_CM3/port.c')
$objects = @()
foreach ($source in $sources) {
    $obj = Join-Path $build (($source.Replace('/','_')) + '.o')
    Run-Tool "$ArmBin/arm-none-eabi-gcc.exe" ($flags + @('-c',"$project/$source",'-o',$obj))
    $objects += $obj
}
$elf = "$build/ecg_freertos.elf"
Run-Tool "$ArmBin/arm-none-eabi-gcc.exe" (@('-mcpu=cortex-m3','-mthumb','-nostdlib') + $objects + @("-T$project/firmware/link.ld",'-Wl,--gc-sections','-Wl,-z,noexecstack',"-Wl,-Map=$build/ecg_freertos.map",'-Wl,--print-memory-usage','-lgcc','-o',$elf))
Run-Tool "$ArmBin/arm-none-eabi-objcopy.exe" @('-O','binary',$elf,"$build/ecg_freertos.bin")
Run-Tool "$ArmBin/arm-none-eabi-objcopy.exe" @('-O','ihex',$elf,"$build/ecg_freertos.hex")
Run-Tool "$ArmBin/arm-none-eabi-size.exe" @($elf)
if (!$FirmwareOnly) {
    $hostFlags = @('-std=c11','-O2','-Wall','-Wextra','-Werror',"-I$project/common","-I$project/host")
    $common = @("$project/common/protocol.c","$project/common/history.c","$project/host/receiver.c")
    Run-Tool "$NativeBin/gcc.exe" @('-std=c11','-O2','-Wall','-Wextra','-Werror',"$project/tools/make_wave.c",'-lm','-o',"$build/make_wave.exe")
    Run-Tool "$NativeBin/gcc.exe" ($hostFlags + $common + @("$project/tests/test_core.c",'-o',"$build/test_core.exe"))
    Run-Tool "$build/test_core.exe" @()
    $serial = @("$project/host/serial.c","$project/host/session.c")
    Run-Tool "$NativeBin/gcc.exe" ($hostFlags + $common + $serial + @("$project/tests/test_session.c",'-o',"$build/test_session.exe"))
    Run-Tool "$build/test_session.exe" @()
    Run-Tool "$NativeBin/gcc.exe" ($hostFlags + $common + $serial + @("$project/host/board_test.c",'-o',"$build/board_test.exe"))
    Run-Tool "$NativeBin/gcc.exe" ($hostFlags + $common + $serial + @("$project/host/viewer.c",'-mwindows','-lgdi32','-lcomdlg32','-o',"$build/ecg_viewer.exe"))
}
Write-Host "Built: $build"
