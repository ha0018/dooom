param(
    [string]$QtRoot = 'C:\Qt\6.7.3\msvc2019_64',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$SkipTests,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$qtInstallRoot = Split-Path -Parent (Split-Path -Parent $QtRoot)
$cmakePath = Join-Path $qtInstallRoot 'Tools\CMake_64\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmakePath)) {
    $cmakePath = (Get-Command cmake -ErrorAction Stop).Source
}
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw "Qt SDK not found at $QtRoot. Pass -QtRoot with your MSVC Qt SDK path."
}
$buildDirectory = Join-Path $workspaceRoot 'build'
& $cmakePath -S $workspaceRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" '-DBUILD_TESTING=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmakePath --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$originalTaskPath = $env:PATH
try {
    $env:PATH = "$QtRoot\bin;$env:PATH"
    if (-not $SkipTests) {
        $ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
        & $ctestPath --test-dir $buildDirectory -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
    if ($Package) {
        $releaseDirectory = Join-Path $workspaceRoot 'dist\LogScope'
        New-Item -ItemType Directory -Force -Path $releaseDirectory | Out-Null
        Copy-Item -LiteralPath (Join-Path $buildDirectory "$Configuration\LogScope.exe") -Destination $releaseDirectory -Force
        $deployOption = if ($Configuration -eq 'Release') { '--release' } else { '--debug' }
        & (Join-Path $QtRoot 'bin\windeployqt.exe') $deployOption --no-translations --no-compiler-runtime (Join-Path $releaseDirectory 'LogScope.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
        if ($Configuration -eq 'Release') {
            $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'vswhere is required to locate the MSVC runtime.' }
            $visualStudioRoot = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            $redistRoot = Join-Path $visualStudioRoot 'VC\Redist\MSVC'
            $runtimeVersion = Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
            if (-not $runtimeVersion) { throw 'MSVC redistributable DLLs were not found.' }
            $runtimeDirectory = Join-Path $runtimeVersion.FullName 'x64\Microsoft.VC143.CRT'
            if (-not (Test-Path -LiteralPath $runtimeDirectory)) { throw "CRT directory not found: $runtimeDirectory" }
            Get-ChildItem -LiteralPath $runtimeDirectory -Filter '*.dll' | Copy-Item -Destination $releaseDirectory -Force
        }
        Copy-Item -LiteralPath (Join-Path $workspaceRoot 'README.md') -Destination $releaseDirectory -Force
        Copy-Item -LiteralPath (Join-Path $workspaceRoot 'docs') -Destination $releaseDirectory -Recurse -Force
        Copy-Item -LiteralPath (Join-Path $workspaceRoot 'samples\demo.log') -Destination $releaseDirectory -Force
        Write-Output "Runnable package: $releaseDirectory\LogScope.exe"
    }
} finally { $env:PATH = $originalTaskPath }
