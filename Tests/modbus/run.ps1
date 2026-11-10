#Requires -Version 5.1
[CmdletBinding()]
param([string]$CompilerRoot = 'D:\matlab_to_1014\sys\lcc64\lcc64')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$compiler = Join-Path $CompilerRoot 'bin\lcc64.exe'
$linker = Join-Path $CompilerRoot 'bin\lcclnk64.exe'
Push-Location $projectRoot
try {
    $output = 'MDK-ARM\build\modbus-tests'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $sources = @('HARDLIB/RS485.c', 'HARDLIB/ModBus.c', 'HARDLIB/Filter.c',
                 'App/app_measure.c', 'App/app_modbus.c', 'Tests/modbus/test_modbus.c')
    $objects = @()
    foreach ($source in $sources) {
        $name = [IO.Path]::GetFileNameWithoutExtension($source)
        $object = "$output\$name.obj"
        & $compiler '-noregistrylookup' "-I$CompilerRoot\include64" '-ITests/modbus' '-IHARDLIB' `
            '-IApp' '-ICore/Inc' '-o' $object $source
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
        $objects += $object
    }
    $executable = "$output\test_modbus.exe"
    & $linker "-L$CompilerRoot\lib64" '-subsystem' 'console' '-o' $executable @objects
    if ($LASTEXITCODE -ne 0) { throw 'Test link failed' }
    & ".\$executable"
    if ($LASTEXITCODE -ne 0) { throw 'Modbus tests failed' }
}
finally { Pop-Location }
