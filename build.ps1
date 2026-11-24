param([switch]$Test, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'

$gcc = (Get-Command gcc.exe).Source
$windres = (Get-Command windres.exe).Source
$dist = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $PSScriptRoot 'dist' }
$temp = Join-Path ([IO.Path]::GetTempPath()) ('segnaleschermo-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dist, $temp -Force | Out-Null
$resources = Join-Path $temp 'app.o'
$testExe = Join-Path $temp 'tests.exe'
Push-Location -LiteralPath $PSScriptRoot
try {
    if (!(Test-Path -LiteralPath 'assets/schermo.ico')) {
        & './assets/generate-icon.ps1'
    }
    & $windres --codepage=65001 app.rc $resources
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione delle risorse non riuscita.' }

    $flags = @(
        '-std=c17', '-Wall', '-Wextra', '-Werror', '-O2', '-DUNICODE', '-D_UNICODE',
        '-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00', '-finput-charset=UTF-8'
    )
    $exe = Join-Path $dist 'DisplaySignalInspector.exe'
    $reader = Join-Path $temp 'edid.o'
    & g++.exe -std=c++17 -O2 -Wall -Wextra -Werror -c edid_reader.cpp -o $reader
    if ($LASTEXITCODE) { throw 'Compilazione lettore EDID fallita.' }
    & $gcc @flags -municode -mwindows -s -static-libgcc main.c display.c $reader $resources -static -lstdc++ -lwbemuuid -lole32 -loleaut32 -o $exe
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione di Segnale Schermo non riuscita.' }

    if ($Test) {
        & $gcc @flags tests.c display.c $reader -static -lstdc++ -lwbemuuid -lole32 -loleaut32 -o $testExe
        if ($LASTEXITCODE -ne 0) { throw 'Compilazione dei test non riuscita.' }
        & $testExe
        if ($LASTEXITCODE -ne 0) { throw 'Verifica non riuscita.' }
    }
    Get-Item -LiteralPath $exe | Select-Object FullName, Length
} finally {
    Pop-Location
    foreach ($file in @($testExe, $resources, (Join-Path $temp "edid.o"))) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
    Remove-Item -LiteralPath $temp
}
