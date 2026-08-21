<#
.SYNOPSIS
  打包 TesTTool 预编译（便携）版本：配置+构建，汇总到 dist\ttt-portable-win64\ 并生成 zip。

.DESCRIPTION
  默认使用静态链接（TTT_STATIC_RUNTIME=ON），生成的 exe 不再依赖
  libgcc_s_seh-1.dll / libstdc++-6.dll 等 MinGW 运行库，可直接拷到
  其他 Win10/11 电脑运行（UCRT 为系统自带）
  加 -Dynamic 则保持动态链接，并把所需运行库 DLL 一并拷贝进包

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File package.ps1
  powershell -ExecutionPolicy Bypass -File package.ps1 -Dynamic
  powershell -ExecutionPolicy Bypass -File package.ps1 -NoZip
#>
[CmdletBinding()]
param(
    [string]$MsysRoot = "C:\msys64",          # MSYS2 安装目录
    [string]$Config = "Release",              # Release / Debug
    [string]$BuildDir = "build-portable",     # 独立构建目录（不动现有 build/）
    [string]$OutRoot = "dist",                # 输出根目录
    [switch]$Dynamic,                         # 动态链接+拷贝运行库 DLL（默认静态链接）
    [switch]$NoZip                            # 跳过生成 zip
)
$ErrorActionPreference = "Stop"

$repo  = $PSScriptRoot
$ucrt  = Join-Path $MsysRoot "ucrt64"
$binDir = Join-Path $ucrt "bin"
$cmake    = Join-Path $binDir "cmake.exe"
$ninja    = Join-Path $binDir "ninja.exe"
$objdump  = Join-Path $binDir "objdump.exe"
foreach ($tool in @($cmake, $ninja, $objdump)) {
    if (-not (Test-Path $tool)) { throw "找不到工具: $tool（请确认已安装 MSYS2 UCRT64，或用 -MsysRoot 指定）" }
}

$staticOn = if ($Dynamic) { "OFF" } else { "ON" }
$modeName = if ($Dynamic) { "动态链接(+运行库DLL)" } else { "静态链接(单文件)" }

Write-Host "==> 配置构建目录: $BuildDir  (TTT_STATIC_RUNTIME=$staticOn, $modeName)"
$buildPath = Join-Path $repo $BuildDir
if (Test-Path $buildPath) { Remove-Item $buildPath -Recurse -Force }
& $cmake -S $repo -B $buildPath -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_CXX_COMPILER=$(Join-Path $binDir 'c++.exe')" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DTTT_STATIC_RUNTIME=$staticOn"
if ($LASTEXITCODE -ne 0) { throw "cmake 配置失败 (exit $LASTEXITCODE)" }

Write-Host "==> 构建 (config=$Config)"
& $cmake --build $buildPath --config $Config
if ($LASTEXITCODE -ne 0) { throw "构建失败 (exit $LASTEXITCODE)" }

$buildOut = $buildPath
$pkgName  = "ttt-portable-win64"
$pkgDir   = Join-Path (Join-Path $repo $OutRoot) $pkgName
if (Test-Path $pkgDir) { Remove-Item $pkgDir -Recurse -Force }
New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null

# ---- 拷贝 exe 与帮助文件 ----
foreach ($exe in @("ttt.exe", "ttt-cli.exe")) {
    Copy-Item (Join-Path $buildOut $exe) $pkgDir
}
Copy-Item (Join-Path $buildOut "file_picker_help.txt") $pkgDir
Copy-Item (Join-Path $buildOut "ttt_cli_help.txt")     $pkgDir

# ---- 动态模式：把 MinGW 运行库 DLL 一并拷贝 ----
$systemRe = '^(KERNEL32|USER32|GDI32|SHELL32|ADVAPI32|api-ms-win-crt-.*|ucrtbase|msvcrt|ntdll)\.dll$'
if ($Dynamic) {
    $dllsNeeded = @{}
    foreach ($exe in @("ttt.exe", "ttt-cli.exe")) {
        $deps = & $objdump -p (Join-Path $buildOut $exe) |
                Select-String 'DLL Name' |
                ForEach-Object { ($_.Line -split ':\s*')[-1].Trim() }
        foreach ($d in $deps) {
            if ($d -notmatch $systemRe) { $dllsNeeded[$d] = $true }
        }
    }
    foreach ($dll in @($dllsNeeded.Keys)) {
        $src = Join-Path $binDir $dll
        if (Test-Path $src) {
            Copy-Item $src $pkgDir
            Write-Host "   拷贝运行库: $dll"
        } else {
            Write-Warning "运行库 $dll 未在 $binDir 找到！"
        }
    }
}

# ---- 验证：每个 exe 的非系统 DLL 是否都在包内 ----
Write-Host "==> 验证 DLL 依赖"
$ok = $true
foreach ($exe in @("ttt.exe", "ttt-cli.exe")) {
    $deps = & $objdump -p (Join-Path $pkgDir $exe) |
            Select-String 'DLL Name' |
            ForEach-Object { ($_.Line -split ':\s*')[-1].Trim() }
    $missing = @()
    foreach ($d in $deps) {
        if ($d -match $systemRe) { continue }
        if (-not (Test-Path (Join-Path $pkgDir $d))) { $missing += $d }
    }
    if ($missing.Count) {
        $ok = $false
        Write-Warning "$exe 缺少 DLL: $($missing -join ', ')"
    } else {
        Write-Host "   OK: $exe（无非系统 DLL 缺失）"
    }
}
if (-not $ok) { throw "依赖验证未通过，请检查上方警告" }

# ---- 包内说明 ----
$readme = @"
TesTTool 便携版（$modeName）
============================
- ttt.exe      交互式外壳（add/submit/view/remove/help 转交 ttt-cli 执行）
- ttt-cli.exe  命令行工具
- file_picker_help.txt / ttt_cli_help.txt  帮助文件，须与 exe 同目录

$(if ($Dynamic) { "- 同目录下的 DLL 为 MinGW 运行库，请与 exe 一起分发、勿删除" } else { "- 已静态链接 MinGW 运行库，无需额外 DLL；需 Win10/11（UCRT 为系统自带）" })

使用：双击 ttt.exe，或在命令行运行 ttt-cli help。
"@
Set-Content -Path (Join-Path $pkgDir "README.txt") -Value $readme -Encoding UTF8

# ---- 生成 zip ----
if (-not $NoZip) {
    $zip = Join-Path (Join-Path $repo $OutRoot) "$pkgName.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Write-Host "==> 压缩: $zip"
    Compress-Archive -Path $pkgDir -DestinationPath $zip
    Write-Host "    zip 大小: $([math]::Round((Get-Item $zip).Length / 1MB, 2)) MB"
}

Write-Host ""
Write-Host "打包完成: $pkgDir"
Get-ChildItem $pkgDir | Sort-Object Name | ForEach-Object {
    Write-Host ("   {0,10:N0} B  {1}" -f $_.Length, $_.Name)
}
