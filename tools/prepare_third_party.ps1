param(
    [string]$SourceDir = "",
    [ValidateSet("all", "windows", "linux", "macos")]
    [string]$PackageSet = "all",
    [string]$WebrtcPackage = $env:WEBRTC_PACKAGE,
    [string]$FfmpegPackage = $env:FFMPEG_PACKAGE,
    [switch]$SkipDownload
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DownloadDir = Join-Path $RepoRoot "third_party/_downloads"
$WebrtcDir = Join-Path $RepoRoot "third_party/webrtc"
$FfmpegDir = Join-Path $RepoRoot "third_party/ffmpeg-builds"
$RunningOnWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows)

if (-not $SourceDir -and $env:DEPENDENCY_SOURCE) {
    $SourceDir = $env:DEPENDENCY_SOURCE
}

New-Item -ItemType Directory -Force -Path $DownloadDir, $WebrtcDir, $FfmpegDir | Out-Null

if (-not $WebrtcPackage) {
    throw "WebrtcPackage is required; select the exact platform/architecture/ABI package"
}
$WebrtcManifest = "libwebrtc-manifest.json"
$WebrtcReleaseUrl = "https://github.com/wxalh/libwebrtc_build/releases/latest/download"
$WebrtcResolver = Join-Path $PSScriptRoot "resolve_webrtc_package.py"
$FfmpegReleaseUrl = "https://github.com/wxalh/FFmpeg-Builds/releases/download/latest"
$FfmpegChecksums = "checksums.sha256"

$FfmpegPackages = @(
    @{ Set = "windows"; Name = "ffmpeg-n7.1-latest-win32-lgpl-shared-7.1.zip" },
    @{ Set = "windows"; Name = "ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip" },
    @{ Set = "windows"; Name = "ffmpeg-n8.1-latest-winarm64-lgpl-shared-8.1.zip" },
    @{ Set = "linux"; Name = "ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1.tar.xz" },
    @{ Set = "linux"; Name = "ffmpeg-n8.1-latest-linuxarm64-lgpl-shared-8.1.tar.xz" },
    @{ Set = "linux"; Name = "ffmpeg-n8.1-latest-linuxarmhf-lgpl-shared-8.1.tar.xz" }
)

function Resolve-SourceFile([string]$Name, [Nullable[Int64]]$ExpectedSize = $null) {
    if ($SourceDir) {
        $candidate = Join-Path $SourceDir $Name
        if (Test-Path $candidate) {
            if ($ExpectedSize -and (Get-Item -LiteralPath $candidate).Length -ne $ExpectedSize) {
                Write-Host "Ignoring stale local package: $Name"
            } else {
            return (Resolve-Path $candidate).Path
            }
        }
    }

    $cached = Join-Path $DownloadDir $Name
    if (Test-Path $cached) {
        if ($ExpectedSize -and (Get-Item -LiteralPath $cached).Length -ne $ExpectedSize) {
            Remove-Item -LiteralPath $cached -Force
            return ""
        }
        return (Resolve-Path $cached).Path
    }

    return ""
}

function Save-RemoteFile([string]$Name, [string]$Url) {
    $target = Join-Path $DownloadDir $Name
    $tempTarget = "$target.tmp"
    Write-Host "Downloading $Name"
    if ($RunningOnWindows) {
        & curl.exe -L --fail --retry 5 --retry-delay 5 -o $tempTarget $Url
    } else {
        & curl -L --fail --retry 5 --retry-delay 5 -o $tempTarget $Url
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $tempTarget)) {
        Remove-Item -LiteralPath $tempTarget -Force -ErrorAction SilentlyContinue
        throw "Download failed: $Url"
    }
    Move-Item -LiteralPath $tempTarget -Destination $target -Force
    return $target
}

function Copy-FromSourceOrDownload([string]$Name, [string]$Url, [Nullable[Int64]]$ExpectedSize = $null) {
    $target = Join-Path $DownloadDir $Name
    $source = Resolve-SourceFile $Name $ExpectedSize
    $resolvedTarget = ""
    if (Test-Path $target) {
        $resolvedTarget = (Resolve-Path -LiteralPath $target).Path
    }
    if ($source -and ($source -ne $resolvedTarget)) {
        Copy-Item -LiteralPath $source -Destination $target -Force
        return $target
    }

    if (Test-Path $target) {
        return $target
    }

    if ($SkipDownload) {
        throw "Missing $Name and downloads are disabled"
    }

    return (Save-RemoteFile $Name $Url)
}

function Copy-FromSourceOrRefresh([string]$Name, [string]$Url) {
    $target = Join-Path $DownloadDir $Name
    $source = Resolve-SourceFile $Name
    $resolvedTarget = ""
    if (Test-Path $target) {
        $resolvedTarget = (Resolve-Path -LiteralPath $target).Path
    }
    if ($source -and ($source -ne $resolvedTarget)) {
        Copy-Item -LiteralPath $source -Destination $target -Force
        return $target
    }
    if ($SkipDownload) {
        if (Test-Path $target) {
            return $target
        }
        throw "Missing $Name and downloads are disabled"
    }
    return (Save-RemoteFile $Name $Url)
}

function Get-PublishedChecksum([string]$ChecksumsPath, [string]$Name) {
    $digests = @()
    foreach ($line in Get-Content -LiteralPath $ChecksumsPath) {
        if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+)$' -and $Matches[2] -eq $Name) {
            $digests += $Matches[1].ToLowerInvariant()
        }
    }
    if ($digests.Count -ne 1) {
        throw "FFmpeg checksum list must contain exactly one entry for $Name"
    }
    return $digests[0]
}

function Test-FfmpegArchive([string]$ArchivePath, [string]$ExpectedSha256, [string]$Name) {
    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        return $false
    }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $ArchivePath).Hash.ToLowerInvariant()
    if ($actual -ne $ExpectedSha256) {
        Write-Host "FFmpeg SHA256 mismatch for $Name. Expected $ExpectedSha256, got $actual"
        return $false
    }
    return $true
}

function Get-FfmpegArchive([string]$Name, [string]$ExpectedSha256) {
    $target = Join-Path $DownloadDir $Name
    if ($SourceDir) {
        $candidate = Join-Path $SourceDir $Name
        if (Test-FfmpegArchive $candidate $ExpectedSha256 $Name) {
            Copy-Item -LiteralPath $candidate -Destination $target -Force
        }
    }
    if (-not (Test-FfmpegArchive $target $ExpectedSha256 $Name)) {
        Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
        if ($SkipDownload) {
            throw "Missing current FFmpeg package $Name and downloads are disabled"
        }
        $target = Save-RemoteFile $Name "$FfmpegReleaseUrl/$Name"
    }
    if (-not (Test-FfmpegArchive $target $ExpectedSha256 $Name)) {
        Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
        throw "Downloaded FFmpeg package failed SHA256 verification: $Name"
    }
    return $target
}

function Invoke-DockerTarExtract([string]$ArchivePath, [string]$DestinationPath, [string]$TarArgs) {
    $docker = Get-Command docker -ErrorAction SilentlyContinue
    if (-not $docker) {
        throw "Extracting $ArchivePath requires docker because this Windows tar cannot decode the archive"
    }

    $destinationDocker = $DestinationPath -replace "\\", "/"
    $archiveDocker = $ArchivePath -replace "\\", "/"
    $archiveName = Split-Path -Leaf $ArchivePath
    docker run --rm `
        -v "${destinationDocker}:/out" `
        -v "${archiveDocker}:/deps/${archiveName}:ro" `
        ghcr.io/btbn/ffmpeg-builds/base:latest `
        bash -lc "tar ${TarArgs} -xf /deps/${archiveName} -C /out"
    if ($LASTEXITCODE -ne 0) {
        throw "Archive extraction failed in Docker for $ArchivePath (exit code $LASTEXITCODE)"
    }
}

function Test-NativeTarCanRead([string]$ArchivePath, [string[]]$TarArgs) {
    $tar = Get-Command tar -ErrorAction SilentlyContinue
    if (-not $tar) {
        return $false
    }

    $listArgs = @($TarArgs) + @("-tf", $ArchivePath)
    & $tar.Source @listArgs *> $null
    return $LASTEXITCODE -eq 0
}

function Invoke-TarCommandChecked([string]$ArchivePath, [string[]]$Arguments) {
    $tar = Get-Command tar -ErrorAction SilentlyContinue
    if (-not $tar) {
        throw "Archive extraction failed because tar was not found: $ArchivePath"
    }

    & $tar.Source @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Archive extraction failed for $ArchivePath (exit code $LASTEXITCODE)"
    }
}

function Invoke-TarExtract([string]$ArchivePath, [string]$DestinationPath, [string]$TarArgs) {
    $tarArgList = @()
    if ($TarArgs) {
        $tarArgList = $TarArgs -split "\s+"
    }

    if ($RunningOnWindows) {
        if ($ArchivePath.EndsWith(".tar.zst") -or $ArchivePath.EndsWith(".tar.zst.part-000")) {
            if (Test-NativeTarCanRead $ArchivePath $tarArgList) {
                $extractArgs = @($tarArgList) + @("-xf", $ArchivePath, "-C", $DestinationPath)
                Invoke-TarCommandChecked $ArchivePath $extractArgs
                return
            }
            Invoke-DockerTarExtract $ArchivePath $DestinationPath $TarArgs
            return
        }

        if ($ArchivePath.EndsWith(".tar.xz")) {
            if (Test-NativeTarCanRead $ArchivePath @()) {
                Invoke-TarCommandChecked $ArchivePath @("-xf", $ArchivePath, "-C", $DestinationPath)
                return
            }
            Invoke-DockerTarExtract $ArchivePath $DestinationPath $TarArgs
            return
        }
    }

    if ($TarArgs) {
        $extractArgs = @($tarArgList) + @("-xf", $ArchivePath, "-C", $DestinationPath)
        Invoke-TarCommandChecked $ArchivePath $extractArgs
    } else {
        Invoke-TarCommandChecked $ArchivePath @("-xf", $ArchivePath, "-C", $DestinationPath)
    }
}

function Test-WebrtcReady {
    return (Test-Path (Join-Path $WebrtcDir "cmake/LibWebRTCConfig.cmake")) -or
        (Test-Path (Join-Path $WebrtcDir "LibWebRTCConfig.cmake"))
}

function Test-WebrtcArchive([string]$ArchivePath, [string]$ExpectedSha256, [Int64]$ExpectedSize) {
    if ((Get-Item -LiteralPath $ArchivePath).Length -ne $ExpectedSize) {
        Write-Host "WebRTC package size mismatch for $WebrtcPackage"
        return $false
    }
    $actual = (Get-FileHash -Algorithm SHA256 $ArchivePath).Hash.ToLowerInvariant()
    if ($actual -ne $ExpectedSha256) {
        Write-Host "WebRTC SHA256 mismatch. Expected $ExpectedSha256, got $actual"
        return $false
    }
    return $true
}

function Test-WebrtcPreparedMatches {
    if (-not (Test-WebrtcReady)) {
        return $false
    }
    $metadataPath = Join-Path $WebrtcDir "PACKAGE-METADATA.json"
    if (-not (Test-Path -LiteralPath $metadataPath)) {
        return $false
    }
    try {
        $metadata = Get-Content -Raw -LiteralPath $metadataPath | ConvertFrom-Json
        return [string]$metadata.package -eq $WebrtcPackage
    } catch {
        return $false
    }
}

function Expand-Webrtc([string]$ArchivePath, [string]$ExpectedSha256, [Int64]$ExpectedSize) {
    if (-not (Test-WebrtcArchive $ArchivePath $ExpectedSha256 $ExpectedSize)) {
        throw "WebRTC SHA256 mismatch"
    }

    Write-Host "Extracting WebRTC package: $WebrtcPackage"
    if ($RunningOnWindows) {
        Get-ChildItem -LiteralPath $WebrtcDir -Force | Remove-Item -Recurse -Force
        Invoke-TarExtract $ArchivePath $WebrtcDir "--zstd --strip-components=1"
    } else {
        tar --zstd -xf $ArchivePath -C $WebrtcDir --strip-components=1
        if ($LASTEXITCODE -ne 0) {
            throw "Archive extraction failed for $ArchivePath (exit code $LASTEXITCODE)"
        }
    }
    if (-not (Test-WebrtcReady)) {
        throw "Extracted WebRTC package is incomplete: $ArchivePath"
    }
    if (-not (Test-WebrtcPreparedMatches)) {
        throw "Extracted WebRTC package metadata is missing or mismatched: $ArchivePath"
    }
}

function Expand-ArchiveIfNeeded([string]$ArchivePath, [string]$PackageName, [string]$ExpectedSha256) {
    $targetName = $PackageName -replace "\.zip$","" -replace "\.tar\.xz$",""
    $targetDir = Join-Path $FfmpegDir $targetName
    $markerPath = Join-Path $targetDir ".airan-package-sha256"
    if ((Test-Path (Join-Path $targetDir "include")) -and
        (Test-Path $markerPath) -and
        ((Get-Content -Raw $markerPath).Trim() -eq $ExpectedSha256)) {
        Write-Host "FFmpeg package already prepared: $targetName"
        return
    }

    Write-Host "Extracting FFmpeg package: $PackageName"
    Remove-Item -LiteralPath $targetDir -Recurse -Force -ErrorAction SilentlyContinue
    if ($PackageName.EndsWith(".zip")) {
        Expand-Archive -LiteralPath $ArchivePath -DestinationPath $FfmpegDir -Force
    } else {
        Invoke-TarExtract $ArchivePath $FfmpegDir ""
    }
    Set-Content -LiteralPath $markerPath -Value $ExpectedSha256 -NoNewline
}

$manifestPath = Copy-FromSourceOrRefresh $WebrtcManifest "$WebrtcReleaseUrl/$WebrtcManifest"
$python = $null
foreach ($candidate in @("python3", "python")) {
    $command = Get-Command $candidate -ErrorAction SilentlyContinue
    if (-not $command) {
        continue
    }
    & $command.Source -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)" 2>$null
    if ($LASTEXITCODE -eq 0) {
        $python = $command.Source
        break
    }
}
if (-not $python) {
    throw "Python 3.10 or newer is required to resolve WebRTC release metadata"
}
$selectionJson = & $python $WebrtcResolver --manifest $manifestPath --package $WebrtcPackage
if ($LASTEXITCODE -ne 0) {
    throw "Unable to resolve WebRTC package from release manifest: $WebrtcPackage"
}
$selection = $selectionJson | ConvertFrom-Json
$WebrtcArchive = [string]$selection.asset
$expectedSha256 = [string]$selection.sha256
$expectedSize = [Int64]$selection.size

if (Test-WebrtcPreparedMatches) {
    Write-Host "WebRTC package already prepared"
} else {
    $webrtcPackageUrl = "$WebrtcReleaseUrl/$WebrtcArchive"
    $webrtcArchivePath = Copy-FromSourceOrDownload $WebrtcArchive $webrtcPackageUrl $expectedSize
    if (-not (Test-WebrtcArchive $webrtcArchivePath $expectedSha256 $expectedSize)) {
        Write-Host "Refreshing WebRTC package from latest release"
        Remove-Item -LiteralPath (Join-Path $DownloadDir $WebrtcArchive) -Force -ErrorAction SilentlyContinue
        $webrtcArchivePath = Save-RemoteFile $WebrtcArchive $webrtcPackageUrl
    }
    Expand-Webrtc $webrtcArchivePath $expectedSha256 $expectedSize
}

$ffmpegChecksumsPath = Copy-FromSourceOrRefresh $FfmpegChecksums "$FfmpegReleaseUrl/$FfmpegChecksums"
$ffmpegPackageMatched = -not $FfmpegPackage
foreach ($package in $FfmpegPackages) {
    if ($PackageSet -ne "all" -and $PackageSet -ne $package.Set) {
        continue
    }
    if ($FfmpegPackage -and $FfmpegPackage -ne $package.Name) {
        continue
    }
    $ffmpegPackageMatched = $true
    $expectedFfmpegSha256 = Get-PublishedChecksum $ffmpegChecksumsPath $package.Name
    $archive = Get-FfmpegArchive $package.Name $expectedFfmpegSha256
    Expand-ArchiveIfNeeded $archive $package.Name $expectedFfmpegSha256
}
if (-not $ffmpegPackageMatched) {
    throw "Unknown FFmpeg package selection: $FfmpegPackage"
}

Write-Host "Third-party packages are ready"
