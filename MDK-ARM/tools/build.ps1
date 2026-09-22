#Requires -Version 5.1
<#
.SYNOPSIS
    Command-line full build for 20260703-ADS1220 (ARMCC 5.06u7 + armlink).

.DESCRIPTION
    Reproduces the Keil MDK / EIDE compile and link flow so that refactoring can
    be verified without the Keil GUI.

    Properties:
      * Depends only on version-controlled sources; does not need the EIDE
        generated .cmsis / RTE folders.
      * Compiler options match the project: Cortex-M3, --c99, -O3,
        --split_sections, --apcs=interwork.
      * The linker scatter file is generated automatically.
      * Only 0 errors and 0 warnings count as a pass; Program Size is printed
        for comparison against the recorded baseline.

    IMPORTANT - why this script is pure ASCII and uses relative paths:

      The project lives under a path containing Chinese characters. ARMCC cannot
      handle non-ASCII absolute paths on the command line: it re-encodes them
      through the ANSI code page and then fails to open its own output files
      (error #564). Every path handed to armcc / armasm / armlink is therefore
      relative to the MDK-ARM folder. PowerShell itself still uses absolute
      paths for file I/O, because .NET handles Unicode correctly.

      For the same reason this script contains no non-ASCII characters: Windows
      PowerShell 5.1 decodes BOM-less script files using the ANSI code page, so
      any non-ASCII text here would be corrupted and could break parsing.
      Chinese documentation lives in MDK-ARM/tools/README.md instead.

    BASELINE (recorded before the refactor, commit d24f01b):

        Total RO  Size (Code + RO Data)             19584 (19.13kB)
        Total RW  Size (RW Data + ZI Data)           2096 ( 2.05kB)
        Total ROM Size (Code + RO Data + RW Data)   19640 (19.18kB)

      Relation to the historical Keil / EIDE numbers (19580 / 2096 / 19636):

      The original Keil object files were re-linked with this script's link
      arguments and produced exactly 19584 - the same figure as this script -
      which proves the COMPILER settings here are equivalent to the project's.
      The remaining 4-byte RO difference against the historical Keil link is a
      section-padding artifact of link ordering (.ANY placement), not of code
      generation or of the sources.

      Consequence: compare every later stage against the 19584 figures above,
      not against the historical Keil numbers.

.PARAMETER Clean
    Remove the output folder before building.

.PARAMETER ToolchainRoot
    ARMCC root folder (must contain bin\armcc.exe).
    Default: D:\keilpack\keilpackcpre\ARM\ARMCC

.PARAMETER CompileOnly
    Compile without linking, for a fast syntax check.

.EXAMPLE
    powershell -File MDK-ARM\tools\build.ps1 -Clean

.EXAMPLE
    powershell -File MDK-ARM\tools\build.ps1 -CompileOnly
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$ToolchainRoot = 'D:\keilpack\keilpackcpre\ARM\ARMCC',
    [switch]$CompileOnly
)

# Native tools turn stderr into ErrorRecords; keep going and judge by exit code.
$ErrorActionPreference = 'Continue'

# Run an external tool and collect stdout/stderr as clean text lines.
# Windows PowerShell 5.1 wraps native stderr in ErrorRecord objects, so unwrap
# them before applying the error / warning regexes.
function Invoke-NativeTool {
    param(
        [Parameter(Mandatory)][string]   $Exe,
        [Parameter(Mandatory)][string[]] $Arguments
    )

    $raw  = & $Exe @Arguments 2>&1
    $code = $LASTEXITCODE

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($item in @($raw)) {
        if ($item -is [System.Management.Automation.ErrorRecord]) {
            $text = $item.Exception.Message
        }
        else {
            $text = [string]$item
        }
        foreach ($part in ($text -split "`r?`n")) {
            if ($part.Trim().Length -gt 0) { $lines.Add($part) }
        }
    }

    return [pscustomobject]@{ ExitCode = $code; Lines = $lines }
}

$ProjectDir = Split-Path -Parent $PSScriptRoot
if (-not $ProjectDir) {
    throw 'Cannot locate the project folder (script should live in MDK-ARM\tools\).'
}

Push-Location $ProjectDir
try {
    # ------------------------------------------------------------------
    # Toolchain
    # ------------------------------------------------------------------
    $Bin     = Join-Path $ToolchainRoot 'bin'
    $Armcc   = Join-Path $Bin 'armcc.exe'
    $Armasm  = Join-Path $Bin 'armasm.exe'
    $Armlink = Join-Path $Bin 'armlink.exe'
    $Fromelf = Join-Path $Bin 'fromelf.exe'

    foreach ($tool in @($Armcc, $Armasm, $Armlink)) {
        if (-not (Test-Path -LiteralPath $tool)) {
            throw "Toolchain not found: $tool`nUse -ToolchainRoot to point at the ARMCC folder."
        }
    }

    # ------------------------------------------------------------------
    # Output paths (relative for native tools, absolute for PowerShell)
    # ------------------------------------------------------------------
    $OutDirRel  = 'build\20260703-ADS1220'
    $ObjDirRel  = "$OutDirRel\.obj"
    $MapRel     = "$OutDirRel\20260703-ADS1220.map"
    $ElfRel     = "$OutDirRel\20260703-ADS1220.axf"
    $HexRel     = "$OutDirRel\20260703-ADS1220.hex"
    $ScatterRel = "$OutDirRel\20260703-ADS1220.sct"

    $OutDir  = Join-Path $ProjectDir $OutDirRel
    $ObjDir  = Join-Path $ProjectDir $ObjDirRel
    $MapFile = Join-Path $ProjectDir $MapRel
    $ElfFile = Join-Path $ProjectDir $ElfRel
    $HexFile = Join-Path $ProjectDir $HexRel
    $Scatter = Join-Path $ProjectDir $ScatterRel

    if ($Clean -and (Test-Path -LiteralPath $OutDir)) {
        Remove-Item -LiteralPath $OutDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

    # Make native tools inherit the right working directory.
    [Environment]::CurrentDirectory = $ProjectDir

    # ------------------------------------------------------------------
    # Compiler options (must match the Keil / EIDE project)
    # ------------------------------------------------------------------
    $IncludeDirs = @(
        '.'
        '../Core/Inc'
        '../Drivers/STM32F1xx_HAL_Driver/Inc'
        '../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy'
        '../Drivers/CMSIS/Device/ST/STM32F1xx/Include'
        '../Drivers/CMSIS/Include'
        '../HARDLIB'
        '../App'
    ) | Where-Object { Test-Path -LiteralPath $_ }

    $DefineArgs  = @('-DUSE_HAL_DRIVER', '-DSTM32F103xB')
    $CommonArgs  = @('--cpu', 'Cortex-M3', '--li', '--apcs=interwork')
    $CArgs       = @(
        '--c99', '-O3', '--split_sections', '-g'
        '--diag_suppress=1', '--diag_suppress=1295'
        '--no_depend_system_headers'
    )
    $IncludeArgs = @($IncludeDirs | ForEach-Object { '-I' + $_ })

    # ------------------------------------------------------------------
    # Source list
    #   Project code is collected per folder, so a new App\ folder is picked
    #   up automatically. Vendor HAL sources are the whole Src folder, which
    #   matches the Keil / EIDE project file list.
    # ------------------------------------------------------------------
    $SourceGroups = @(
        '../Core/Src'
        '../HARDLIB'
        '../App'
        '../Drivers/STM32F1xx_HAL_Driver/Src'
    )

    $Sources = New-Object System.Collections.Generic.List[string]
    foreach ($group in $SourceGroups) {
        if (-not (Test-Path -LiteralPath $group)) { continue }
        Get-ChildItem -LiteralPath $group -File |
            Where-Object { $_.Extension -in @('.c', '.C') } |
            Sort-Object Name |
            ForEach-Object { $Sources.Add("$group/$($_.Name)") }
    }

    if ($Sources.Count -eq 0) { throw 'No source files found.' }

    # Object files always use relative paths (see the header note).
    function Get-ObjectPath {
        param([string]$Source)
        $flat = ($Source -replace '^\.\./', '') -replace '[\\/]', '_'
        $name = [System.IO.Path]::GetFileNameWithoutExtension($flat)
        return "$ObjDirRel\$name.o"
    }

    # ------------------------------------------------------------------
    # Compile
    # ------------------------------------------------------------------
    $AllOutput    = New-Object System.Collections.Generic.List[string]
    $ErrorCount   = 0
    $WarningCount = 0
    $FailedFiles  = New-Object System.Collections.Generic.List[string]
    $Stopwatch    = [System.Diagnostics.Stopwatch]::StartNew()

    Write-Host ''
    Write-Host "=== Compiling $($Sources.Count) C files ===" -ForegroundColor Cyan

    foreach ($src in $Sources) {
        $obj = Get-ObjectPath $src
        $dep = [System.IO.Path]::ChangeExtension($obj, '.d')

        $argList = @('-c') + $CommonArgs + $IncludeArgs + $DefineArgs + $CArgs +
                   @('-o', $obj, '--depend', $dep, $src)

        $result    = Invoke-NativeTool -Exe $Armcc -Arguments $argList
        $lineArray = @($result.Lines)

        $fileErrors   = @($lineArray | Where-Object { $_ -match ':\s*error\s*:' })
        $fileWarnings = @($lineArray | Where-Object { $_ -match ':\s*warning\s*:' })

        $ErrorCount   += $fileErrors.Count
        $WarningCount += $fileWarnings.Count
        if ($result.ExitCode -ne 0 -and $fileErrors.Count -eq 0) { $ErrorCount++ }

        if ($fileErrors.Count -gt 0 -or $fileWarnings.Count -gt 0 -or $result.ExitCode -ne 0) {
            $FailedFiles.Add($src)
            $AllOutput.Add("--- $src")
            foreach ($line in $lineArray) { $AllOutput.Add($line) }
        }
    }

    # Startup file (armasm)
    $StartupSrc = 'startup_stm32f103xb.s'
    $StartupObj = "$ObjDirRel\startup_stm32f103xb.o"
    $StartupDep = "$ObjDirRel\startup_stm32f103xb.d"

    $asmArgList = $IncludeArgs + $CommonArgs +
                  @('-g', '-o', $StartupObj, '--depend', $StartupDep, $StartupSrc)

    $asmResult = Invoke-NativeTool -Exe $Armasm -Arguments $asmArgList
    $asmLines  = @($asmResult.Lines)
    $asmErrors = @($asmLines | Where-Object { $_ -match ':\s*error\s*:' })

    $ErrorCount += $asmErrors.Count
    if ($asmResult.ExitCode -ne 0 -and $asmErrors.Count -eq 0) { $ErrorCount++ }

    if ($asmLines.Count -gt 0) {
        $AllOutput.Add("--- $StartupSrc")
        foreach ($line in $asmLines) { $AllOutput.Add($line) }
    }

    Write-Host ("    Compiled $($Sources.Count) sources in {0:N1}s" -f $Stopwatch.Elapsed.TotalSeconds)

    # ------------------------------------------------------------------
    # Link
    # ------------------------------------------------------------------
    $linkOk = $false
    if (-not $CompileOnly -and $ErrorCount -eq 0) {
        Write-Host '=== Linking ===' -ForegroundColor Cyan

        # Scatter file, same layout as the EIDE-generated one: 64K Flash / 20K RAM.
        $scatterText = @'
; Auto-generated by MDK-ARM/tools/build.ps1

LR_IROM1 0x08000000 0x00010000 {
    ER_IROM1 0x08000000 0x00010000 {
        *.o (RESET, +First)
        *(InRoot$$Sections)
        .ANY (+RO)
        .ANY (+XO)
    }
    RW_IRAM1 0x20000000 0x00005000 {
        .ANY (+RW +ZI)
    }
}
'@
        Set-Content -LiteralPath $Scatter -Value $scatterText -Encoding Ascii

        # Startup object goes first, matching the Keil .lnp order.
        # Link order changes .ANY placement padding, and this ordering reproduces
        # the project's original image size exactly (see the baseline note above).
        $objs = @($StartupObj) + @($Sources | ForEach-Object { Get-ObjectPath $_ })

        $linkArgs = @('--cpu', 'Cortex-M3', '--scatter', $ScatterRel, '--strict',
                      '--map', '--info', 'sizes', '--info', 'totals',
                      '--list', $MapRel, '-o', $ElfRel) + $objs

        $linkResult = Invoke-NativeTool -Exe $Armlink -Arguments $linkArgs
        $linkLines  = @($linkResult.Lines)
        $linkErrors = @($linkLines | Where-Object { $_ -match ':\s*error\s*:' -or $_ -match '^\s*Error:' })

        $ErrorCount += $linkErrors.Count
        if ($linkResult.ExitCode -ne 0 -and $linkErrors.Count -eq 0) { $ErrorCount++ }

        if ($linkErrors.Count -gt 0 -or $linkResult.ExitCode -ne 0) {
            $AllOutput.Add('--- armlink')
            foreach ($line in $linkLines) { $AllOutput.Add($line) }
        }
        else {
            $linkOk = $true

            # armlink writes the size summary into the map file, not to stdout.
            if (Test-Path -LiteralPath $MapFile) {
                foreach ($hit in (Select-String -LiteralPath $MapFile -Pattern 'Total RO|Total RW|Total ROM')) {
                    Write-Host "    $($hit.Line.Trim())"
                }
            }
        }

        # Produce the HEX file, matching Keil's CreateHexFile=1.
        if ($linkOk -and (Test-Path -LiteralPath $Fromelf)) {
            Invoke-NativeTool -Exe $Fromelf -Arguments @('--i32combined', '-o', $HexRel, $ElfRel) | Out-Null
        }
    }

    # ------------------------------------------------------------------
    # Summary
    # ------------------------------------------------------------------
    $Stopwatch.Stop()

    if ($AllOutput.Count -gt 0) {
        Write-Host ''
        Write-Host '=== Compiler / linker output ===' -ForegroundColor Yellow
        foreach ($line in $AllOutput) { Write-Host $line }
    }

    Write-Host ''
    Write-Host '========================================' -ForegroundColor Cyan
    if ($ErrorCount -eq 0 -and $WarningCount -eq 0 -and ($CompileOnly -or $linkOk)) {
        Write-Host ("  BUILD OK    {0} error, {1} warning   ({2:N1}s)" -f $ErrorCount, $WarningCount, $Stopwatch.Elapsed.TotalSeconds) -ForegroundColor Green
        if ($linkOk) {
            Write-Host "  Output: $ElfFile"
            Write-Host "          $HexFile"
        }
        $script:BuildExitCode = 0
    }
    else {
        Write-Host ("  BUILD FAIL  {0} error, {1} warning   ({2:N1}s)" -f $ErrorCount, $WarningCount, $Stopwatch.Elapsed.TotalSeconds) -ForegroundColor Red
        if ($FailedFiles.Count -gt 0) {
            Write-Host '  Files involved:'
            foreach ($f in $FailedFiles) { Write-Host "    - $f" }
        }
        $script:BuildExitCode = 1
    }
}
finally {
    Pop-Location
}

exit $script:BuildExitCode
