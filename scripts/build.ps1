# Enter MSVC dev environment and build.
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $repoRoot

$cmakeBin = Join-Path ${env:ProgramFiles} 'CMake\bin'
if (Test-Path $cmakeBin) { $env:Path = "$cmakeBin;$env:Path" }
if ($env:LOCALAPPDATA) {
    $wingetLinks = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'
    if (Test-Path $wingetLinks) { $env:Path = "$wingetLinks;$env:Path" }
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vcvarsCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
        (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat')
        (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat')
        (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat')
    )
    $vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $vcvars) { throw 'Could not find vcvars64.bat. Install Visual Studio 2022 Build Tools with C++ workload.' }

    $envDump = & cmd /c "`"$vcvars`" >NUL && set"
    foreach ($line in $envDump) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2]
        }
    }
}

Write-Host '--- tool versions ---'
& cl.exe 2>&1 | Select-Object -First 1
& cmake.exe --version | Select-Object -First 1
if (Get-Command ninja.exe -ErrorAction SilentlyContinue) { & ninja.exe --version }

Write-Host '--- configure ---'
if (Test-Path build) { Remove-Item -Recurse -Force build }
& cmake.exe -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }

Write-Host '--- build ---'
& cmake.exe --build build --config Release 2>&1
$exit = $LASTEXITCODE
Write-Host "--- exit $exit ---"
if (Test-Path 'build\WorldClock.exe') {
    $fi = Get-Item 'build\WorldClock.exe'
    Write-Host ("OK: {0}  {1:N0} bytes" -f $fi.FullName, $fi.Length)
}
exit $exit
