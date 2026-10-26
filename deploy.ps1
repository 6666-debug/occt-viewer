<#
.SYNOPSIS
    一键构建并打包 OCCT Viewer 到 dist/（可独立运行，不依赖 Qt Creator / 开发环境）。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File deploy.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File deploy.ps1 -SkipBuild   # 复用已有构建产物
#>
[CmdletBinding()]
param(
    [string] $QtDir       = 'F:/Qt/6.11.2/msvc2022_64',
    [string] $OcctDir     = 'C:/OpenCASCADE-7.8.0/occt-vc143-64',
    [string] $Occt3rdParty= 'C:/OpenCASCADE-7.8.0/3rdparty-vc14-64',
    [string] $BuildDir    = '',
    [string] $DistDir     = '',
    [string] $Config      = 'Release',
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1 在参数绑定阶段拿不到 $PSScriptRoot，这里再补默认值
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $PSScriptRoot 'build-dist' }
if ([string]::IsNullOrWhiteSpace($DistDir))  { $DistDir  = Join-Path $PSScriptRoot 'dist' }

function Write-Step([string] $msg) {
    Write-Host ""
    Write-Host "==> $msg" -ForegroundColor Cyan
}

function Assert-Path([string] $path, [string] $what) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "$what 不存在：$path"
    }
}

# ---------------------------------------------------------------------------
# 0. 环境检查
# ---------------------------------------------------------------------------
Write-Step '检查工具链'
Assert-Path $QtDir   'Qt 目录'
Assert-Path $OcctDir 'OpenCASCADE 目录'

$cmake = Join-Path $QtDir 'Tools/CMake_64/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $cmd) { throw '找不到 cmake.exe，请通过 -QtDir 指定 Qt 安装目录或把 cmake 加入 PATH。' }
    $cmake = $cmd.Source
}
$windeployqt = Join-Path $QtDir 'bin/windeployqt.exe'
Assert-Path $windeployqt 'windeployqt.exe'
Write-Host "cmake        : $cmake"
Write-Host "windeployqt  : $windeployqt"

# ---------------------------------------------------------------------------
# 1. 配置 + 编译
# ---------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Step "配置 CMake（$BuildDir）"
    & $cmake -S $PSScriptRoot -B $BuildDir `
        -G 'Visual Studio 17 2022' -A x64 `
        -DCMAKE_PREFIX_PATH="$QtDir" `
        -DOCCT_ROOT="$OcctDir" `
        -DOCCT_3RDPARTY="$Occt3rdParty" `
        -DUNTITLED2_BUILD_TESTS=OFF
    if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败（exit $LASTEXITCODE）" }

    Write-Step "编译 $Config"
    & $cmake --build $BuildDir --config $Config --parallel
    if ($LASTEXITCODE -ne 0) { throw "编译失败（exit $LASTEXITCODE）" }
}

$outDir = Join-Path $BuildDir $Config
$exe    = Join-Path $outDir 'untitled2.exe'
Assert-Path $exe '编译产物 untitled2.exe'

# ---------------------------------------------------------------------------
# 2. 准备 dist 目录
# ---------------------------------------------------------------------------
Write-Step "准备 dist 目录（$DistDir）"
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $DistDir 'untitled2.exe') -Force

# ---------------------------------------------------------------------------
# 3. windeployqt：Qt 运行库 + 插件
# ---------------------------------------------------------------------------
Write-Step '运行 windeployqt'
& $windeployqt --release --no-translations --compiler-runtime (Join-Path $DistDir 'untitled2.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败（exit $LASTEXITCODE）" }

# ---------------------------------------------------------------------------
# 4. OpenCASCADE / 第三方 DLL
#    直接复用构建目录里的 DLL（POST_BUILD 已按依赖清单拷贝齐全），
#    再兜底从 3rdparty 补齐 OCCT 运行期会动态加载的几个库。
# ---------------------------------------------------------------------------
Write-Step '复制 OpenCASCADE 与第三方 DLL'
$dllSources = @(
    $outDir,
    (Join-Path $OcctDir 'win64/vc14/bin'),
    (Join-Path $Occt3rdParty 'tbb2021.5-vc14-x64/bin'),
    (Join-Path $Occt3rdParty 'freetype-2.5.5-vc14-64/bin'),
    (Join-Path $Occt3rdParty 'jemalloc-vc14-64/bin'),
    (Join-Path $Occt3rdParty 'gl2ps-1.3.8-vc14-64/bin'),
    (Join-Path $Occt3rdParty 'freeimage-3.17.0-vc14-64/bin'),
    (Join-Path $Occt3rdParty 'ffmpeg-3.3.4-64/bin'),
    (Join-Path $Occt3rdParty 'openvr-1.14.15-64/bin/win64'),
    (Join-Path $Occt3rdParty 'zlib-1.2.8-vc14-64/bin')
)
$copied = 0
foreach ($src in $dllSources) {
    if (-not (Test-Path -LiteralPath $src)) { continue }
    Get-ChildItem -LiteralPath $src -Filter '*.dll' -File -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $DistDir -Force
        $copied++
    }
}
Write-Host "已复制 DLL 次数：$copied"

# MSVC 运行库（windeployqt --compiler-runtime 通常已处理，这里再兜底一次）
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $vsPath = & $vswhere -latest -property installationPath
    $redistRoot = Join-Path $vsPath 'VC/Redist/MSVC'
    if (Test-Path -LiteralPath $redistRoot) {
        $crtDir = Get-ChildItem -LiteralPath $redistRoot -Directory |
                  Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
                  Sort-Object Name -Descending |
                  Select-Object -First 1 |
                  ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' }
        if ($crtDir -and (Test-Path -LiteralPath $crtDir)) {
            Get-ChildItem -LiteralPath $crtDir -Filter '*.dll' -File | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $DistDir -Force
            }
            Write-Host "已复制 MSVC CRT：$crtDir"
        }
    }
}

# ---------------------------------------------------------------------------
# 5. 独立运行验证：清空 PATH 后跑 --smoke，确认不依赖开发环境
# ---------------------------------------------------------------------------
Write-Step '独立运行验证（PATH 仅保留 system32，模型加载成功返回 0）'
$savedPath = $env:PATH
$savedStderr = $env:QT_ASSUME_STDERR_HAS_CONSOLE
$env:PATH = "$env:SystemRoot\system32;$env:SystemRoot"
$env:QT_ASSUME_STDERR_HAS_CONSOLE = '1'
$errFile = Join-Path $env:TEMP 'occtviewer_smoke.err'
try {
    $proc = Start-Process -FilePath (Join-Path $DistDir 'untitled2.exe') `
        -ArgumentList '--smoke' `
        -WorkingDirectory $DistDir `
        -Wait -PassThru `
        -RedirectStandardError $errFile
} finally {
    $env:PATH = $savedPath
    $env:QT_ASSUME_STDERR_HAS_CONSOLE = $savedStderr
}
Get-Content $errFile -ErrorAction SilentlyContinue |
    Where-Object { $_ -match 'AUTOLOAD|XCAF|UNIT|SCREENSHOT|ERROR|错误' }

if ($proc.ExitCode -eq 0) {
    Write-Host ""
    Write-Host "打包成功：$DistDir" -ForegroundColor Green
    exit 0
} else {
    Write-Host ""
    Write-Warning "冒烟测试退出码 $($proc.ExitCode)（-1073741515 表示缺少 DLL），请检查 dist/ 中的依赖。"
    exit 1
}