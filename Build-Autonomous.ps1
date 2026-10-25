param(
    [ValidateSet('new','djules')][string]$TestRoute = 'new',
    [string]$DependencyRoot = '',
    [string]$BuildRoot = '',
    [switch]$RunTests
)
# Native compiler diagnostics on stderr are not PowerShell failures.
# Every configure/build/test exit code is checked explicitly below.
$ErrorActionPreference = 'Continue'
$sourceRoot = $PSScriptRoot
if (-not $DependencyRoot) { $DependencyRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot '../.local/ofxr-autonomous-deps')) }
if (-not $BuildRoot) { $BuildRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot "../builds/ofxr-autonomous-$TestRoute")) }
$cmake = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
& $cmake -S $sourceRoot -B $BuildRoot -G 'Visual Studio 17 2022' -A x64 "-DXRFG_DEPENDENCY_ROOT=$DependencyRoot" "-DXRFG_TEST_NVIDIA_ROUTE=$TestRoute" "-DXRFG_AFW_RUNTIME=$DependencyRoot/runtime/afw-beta6/PDAFWPlugin.dll" -DXRFG_BUILD_TESTS=ON
if ($LASTEXITCODE) { throw "CMake configure failed: $LASTEXITCODE" }
& $cmake --build $BuildRoot --config Release --parallel
if ($LASTEXITCODE) { throw "Release build failed: $LASTEXITCODE" }
if ($RunTests) {
    & $ctest --test-dir $BuildRoot -C Release --output-on-failure --timeout 60
    if ($LASTEXITCODE) { throw "CTest failed: $LASTEXITCODE" }
}
