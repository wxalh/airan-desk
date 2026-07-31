param(
    [Parameter(Mandatory = $true)]
    [string]$ConfigurePreset,

    [string]$BuildPreset = "",
    [string]$PackageName = "",
    [string]$Configuration = "Release",
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$RunningOnWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows)
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake -and $RunningOnWindows) {
    $vsCmake = "C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    if (Test-Path $vsCmake) {
        $cmake = Get-Item $vsCmake
    }
}
if (-not $cmake) {
    throw "cmake was not found"
}

function Invoke-CheckedNative {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Command,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

if (-not $BuildPreset) {
    $BuildPreset = $ConfigurePreset
    if ($ConfigurePreset.StartsWith("win")) {
        $BuildPreset = "$ConfigurePreset-release"
    }
}

if (-not $PackageName) {
    $PackageName = $ConfigurePreset
}

Push-Location $RepoRoot
try {
    $releaseDir = Join-Path $RepoRoot "out/build/$ConfigurePreset/release"
    $officialCMakeArgs = @($CMakeArgs) + @(
        "-DAIRAN_ENABLE_FFMPEG_RUNTIME=ON"
    )
    Invoke-CheckedNative $cmake --preset $ConfigurePreset @officialCMakeArgs
    Invoke-CheckedNative $cmake --build --preset $BuildPreset --config $Configuration --parallel

    if (-not (Test-Path $releaseDir)) {
        throw "Release output directory not found: $releaseDir"
    }

    $requiredReleaseFiles = @(
        "LICENSE",
        "README.md",
        "README.en.md",
        "PRIVACY.md",
        "THIRD_PARTY_SOURCE_OFFER.md",
        "THIRD_PARTY_SOURCE_OFFER.zh-CN.md",
        "licenses/Third-Party-Notices.md",
        "licenses/third_party_notices.zh-CN.md",
        "licenses/Cisco-OpenH264-BINARY_LICENSE.txt",
        "licenses/FFmpeg-LICENSE.txt",
        "licenses/FFmpeg-source-revision.txt",
        "licenses/FFmpeg-builds-revision.txt",
        "licenses/FFmpeg-build-configuration.txt",
        "licenses/FFmpeg-dependencies.txt",
        "licenses/FFmpeg-SHA256SUMS",
        "licenses/FFmpeg-SOURCE_URLS.txt",
        "licenses/WebRTC-LICENSE.txt",
        "licenses/WebRTC-PATENTS.txt",
        "licenses/WebRTC-Third-Party-Licenses.txt",
        "licenses/WebRTC-args.gn",
        "licenses/WebRTC-package.sha256",
        "licenses/WebRTC-source-revision.txt",
        "licenses/spdlog-LICENSE.txt",
        "licenses/fmt-LICENSE.rst",
        "licenses/libvterm-LICENSE.txt",
        "licenses/Qt-LGPLv3.txt",
        "licenses/Qt-GPLv3.txt",
        "script/airan-notify.sh",
        "script/airan-notify.bat",
        "script/README.md",
        "script/README.zh-CN.md"
    )
    foreach ($requiredFile in $requiredReleaseFiles) {
        if (-not (Test-Path (Join-Path $releaseDir $requiredFile))) {
            throw "Package payload is missing required file: $requiredFile"
        }
    }
    $releaseFiles = Get-ChildItem -LiteralPath $releaseDir -File -Recurse
    $avcodecRuntime = $releaseFiles | Where-Object {
        $_.Name -match '(?i)^avcodec(?:[-_.].*)?\.dll$' -or
        $_.Name -match '(?i)^libavcodec(?:[-_.].*)?\.dylib$' -or
        $_.Name -match '(?i)^libavcodec\.so(?:\..*)?$'
    }
    if (-not $avcodecRuntime) {
        throw "Official package does not contain the required FFmpeg avcodec runtime"
    }
    $forbiddenOpenH264 = $releaseFiles | Where-Object {
        $_.Name -match '(?i)^(lib)?openh264.*\.(dll|dylib|a|lib)$' -or
        $_.Name -match '(?i)^libopenh264.*\.so(?:\..*)?$'
    }
    if ($forbiddenOpenH264) {
        throw "Package contains a forbidden OpenH264 binary: $($forbiddenOpenH264.Name -join ', ')"
    }
    $opensslRuntime = Get-ChildItem -LiteralPath $releaseDir -File -Recurse | Where-Object {
        $_.Name -match '(?i)^(lib)?(ssl|crypto)(?:[-_.].*)?\.(dll|dylib)$' -or
        $_.Name -match '(?i)^lib(ssl|crypto)\.so(?:\..*)?$'
    }
    if ($opensslRuntime -and -not (Test-Path (Join-Path $releaseDir "licenses/OpenSSL-LICENSE.txt"))) {
        throw "Package contains OpenSSL runtime libraries but licenses/OpenSSL-LICENSE.txt is missing"
    }
    $libvaRuntime = Get-ChildItem -LiteralPath (Join-Path $releaseDir "lib") -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '(?i)^libva\.so' }
    if ($libvaRuntime) {
        foreach ($requiredLibvaFile in @(
            "licenses/libva-LICENSE.txt",
            "licenses/libva-source-revision.txt",
            "licenses/libva-source-url.txt",
            "licenses/libva-build-configuration.txt",
            "licenses/libva-SHA256SUMS")) {
            if (-not (Test-Path (Join-Path $releaseDir $requiredLibvaFile))) {
                throw "Package contains libva runtime but is missing $requiredLibvaFile"
            }
        }
    }
    if (-not $RunningOnWindows) {
        Invoke-CheckedNative chmod 0755 (Join-Path $releaseDir "script/airan-notify.sh")
    }
    if (-not $RunningOnWindows -and $ConfigurePreset.StartsWith("linux-")) {
        $requiredLinuxFiles = @(
            "airan-desk",
            "airan-desk-wrapper.sh",
            "install-linux.sh",
            "remove-linux.sh",
            "60-airan-desk-uinput.rules",
            "airan-desk.desktop",
            "airan-desk-autostart.desktop",
            "conf/common.ini"
        )
        foreach ($requiredFile in $requiredLinuxFiles) {
            if (-not (Test-Path (Join-Path $releaseDir $requiredFile))) {
                throw "Linux package payload is missing required file: $requiredFile"
            }
        }
        if (-not (Test-Path (Join-Path $releaseDir "icons/hicolor"))) {
            throw "Linux package payload is missing required directory: icons/hicolor"
        }
        Invoke-CheckedNative chmod +x (Join-Path $releaseDir "install-linux.sh")
        Invoke-CheckedNative chmod +x (Join-Path $releaseDir "remove-linux.sh")
    }

    if ($RunningOnWindows) {
        $releaseExecutable = Join-Path $releaseDir "airan-desk.exe"
        if (-not (Test-Path $releaseExecutable)) {
            throw "Windows release executable is missing: $releaseExecutable"
        }
    }

    $packageDir = Join-Path $RepoRoot "out/packages"
    New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

    $ext = ".tar.gz"
    if ($RunningOnWindows) {
        $ext = ".zip"
    }
    $packagePath = Join-Path $packageDir "$PackageName$ext"
    $temporaryPackagePath = Join-Path $packageDir ".$PackageName.$([guid]::NewGuid().ToString('N')).tmp$ext"
    $stagingDir = $null
    $inspectionDir = $null
    try {
        if ($RunningOnWindows) {
            $stagingDir = Join-Path $env:TEMP "airan-desk-package-$([guid]::NewGuid().ToString('N'))"
            New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null
            Get-ChildItem -LiteralPath $releaseDir -Force | Where-Object {
                -not ($_.PSIsContainer -eq $false -and $_.Extension -ieq '.lib')
            } | ForEach-Object {
                $destination = Join-Path $stagingDir $_.Name
                if ($_.PSIsContainer) {
                    Copy-Item -LiteralPath $_.FullName -Destination $destination -Recurse -Force
                } else {
                    Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
                }
            }
            Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $temporaryPackagePath -Force
        } else {
            Invoke-CheckedNative tar -czf $temporaryPackagePath -C $releaseDir .
        }

        $inspectionDir = Join-Path $env:TEMP "airan-desk-package-inspect-$([guid]::NewGuid().ToString('N'))"
        New-Item -ItemType Directory -Force -Path $inspectionDir | Out-Null
        if ($RunningOnWindows) {
            Expand-Archive -LiteralPath $temporaryPackagePath -DestinationPath $inspectionDir -Force
        } else {
            Invoke-CheckedNative tar -xzf $temporaryPackagePath -C $inspectionDir
        }
        Move-Item -LiteralPath $temporaryPackagePath -Destination $packagePath -Force
    } finally {
        Remove-Item -LiteralPath $temporaryPackagePath -Force -ErrorAction SilentlyContinue
        if ($inspectionDir) {
            Remove-Item -LiteralPath $inspectionDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($stagingDir) {
            Remove-Item -LiteralPath $stagingDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    Write-Host "PACKAGE_PATH=$packagePath"
} finally {
    Pop-Location
}
