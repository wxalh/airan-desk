param(
    [ValidateSet(109, 144)]
    [int]$Milestone = 144,

    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"

$branches = @{
    109 = "5414"
    144 = "7559"
}
$branch = $branches[$Milestone]
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $repoRoot "third_party/licenses/WebRTC-m$Milestone-Third-Party-Licenses.txt"
}

$entries = @(
    @{ Name = "WebRTC"; Base = "https://webrtc.googlesource.com/src"; Ref144 = "refs/branch-heads/7559"; Ref109 = "refs/branch-heads/5414"; Path = "LICENSE" },
    @{ Name = "Abseil"; Repo = "chromium"; Path = "third_party/abseil-cpp/LICENSE" },
    @{ Name = "BoringSSL"; Base = "https://boringssl.googlesource.com/boringssl"; Ref144 = "b94d71f87ff943a617d77f3ff029f9a01a1ec6bc"; Ref109 = "1ccef4908ce04adc6d246262846f3cd8a111fa44"; Path = "LICENSE" },
    @{ Name = "CRC32C"; Base = "https://chromium.googlesource.com/external/github.com/google/crc32c"; Ref144 = "d3d60ac6e0f16780bcfcc825385e1d338801a558"; Ref109 = "fa5ade41ee480003d9c5af6f43567ba22e4e17e6"; Path = "LICENSE" },
    @{ Name = "dav1d"; Base = "https://chromium.googlesource.com/external/github.com/videolan/dav1d"; Ref144 = "fcbc3d1b93f91c709293ed9faea8b7cbcac9030b"; Ref109 = "87f9a81cd770e49394a45deca7a3df41243de00b"; Path = "COPYING" },
    @{ Name = "libaom"; Base = "https://aomedia.googlesource.com/aom"; Ref144 = "5d80673d723a5e2e268b124d81d425053823d875"; Ref109 = "b42e001a9ca9805aff7aaaa270b364a8298c33b4"; Path = "LICENSE" },
    @{ Name = "libjpeg-turbo"; Base = "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo"; Ref144 = "6383cf609c1f63c18af0f59b2738caa0c6c7e379"; Ref109 = "ed683925e4897a84b3bffc5c1414c85b97a129a3"; Path = "LICENSE.md" },
    @{ Name = "libsrtp"; Base = "https://chromium.googlesource.com/chromium/deps/libsrtp"; Ref144 = "a52756acb1c5e133089c798736dd171567df11f5"; Ref109 = "5b7c744eb8310250ccc534f3f86a2015b3887a0a"; Path = "LICENSE" },
    @{ Name = "libvpx"; Base = "https://chromium.googlesource.com/webm/libvpx"; Ref144 = "14cd170a941f88e6fb145ebb873a3c8f87645834"; Ref109 = "91ba9ad08a45e2bd1e0f34658dd4d3fd101b047b"; Path = "LICENSE" },
    @{ Name = "libyuv"; Base = "https://chromium.googlesource.com/libyuv/libyuv"; Ref144 = "957f295ea946cbbd13fcfc46e7066f2efa801233"; Ref109 = "fe9ced6e3c8ae6c69bcc3ebb8505a650d2df30e0"; Path = "LICENSE" },
    @{ Name = "Opus"; Repo = "chromium"; Path = "third_party/opus/src/COPYING" },
    @{ Name = "Perfetto"; Base144 = "https://chromium.googlesource.com/external/github.com/google/perfetto"; Base109 = "https://android.googlesource.com/platform/external/perfetto"; Ref144 = "fdb95badca57068440acc569169f602acee51d7a"; Ref109 = "10498394b9f4b302ee5f56a08e41e7ba7016be44"; Path = "LICENSE" },
    @{ Name = "PFFFT"; Repo = "chromium"; Path = "third_party/pffft/LICENSE" },
    @{ Name = "RNNoise"; Repo = "chromium"; Path = "third_party/rnnoise/COPYING" },
    @{ Name = "WebRTC base64"; Base = "https://webrtc.googlesource.com/src"; Ref144 = "refs/branch-heads/7559"; Ref109 = "refs/branch-heads/5414"; Max = 109; Path = "rtc_base/third_party/base64/LICENSE" },
    @{ Name = "sigslot"; Base = "https://webrtc.googlesource.com/src"; Ref144 = "refs/branch-heads/7559"; Ref109 = "refs/branch-heads/5414"; Path = "rtc_base/third_party/sigslot/LICENSE" },
    @{ Name = "WebRTC FFT"; Base = "https://webrtc.googlesource.com/src"; Ref144 = "refs/branch-heads/7559"; Ref109 = "refs/branch-heads/5414"; Path = "modules/third_party/fft/LICENSE" },
    @{ Name = "G.711"; Base = "https://webrtc.googlesource.com/src"; Ref144 = "refs/branch-heads/7559"; Ref109 = "refs/branch-heads/5414"; Path = "modules/third_party/g711/LICENSE" },
    @{ Name = "G.722"; Base = "https://webrtc.googlesource.com/src"; Ref144 = "refs/branch-heads/7559"; Ref109 = "refs/branch-heads/5414"; Path = "modules/third_party/g722/LICENSE" },
    @{ Name = "Ooura FFT"; Base = "https://webrtc.googlesource.com/src"; Ref144 = "refs/branch-heads/7559"; Ref109 = "refs/branch-heads/5414"; Path = "common_audio/third_party/ooura/LICENSE" },
    @{ Name = "WebRTC sqrt floor"; Base = "https://webrtc.googlesource.com/src"; Ref144 = "refs/branch-heads/7559"; Ref109 = "refs/branch-heads/5414"; Path = "common_audio/third_party/spl_sqrt_floor/LICENSE" }
)

function Get-GitilesText {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Entry
    )

    if ($Entry.Base -or $Entry.Base109 -or $Entry.Base144) {
        $root = if ($Entry.Base) { $Entry.Base } elseif ($Milestone -eq 144) { $Entry.Base144 } else { $Entry.Base109 }
        $ref = if ($Milestone -eq 144) { $Entry.Ref144 } else { $Entry.Ref109 }
        $checkout = Join-Path ([IO.Path]::GetTempPath()) "airan-license-$([Guid]::NewGuid().ToString('N'))"
        try {
            & git init --quiet $checkout
            $fetched = $false
            for ($attempt = 1; $attempt -le 3 -and -not $fetched; $attempt++) {
                & git -C $checkout fetch --quiet --depth 1 --filter=blob:none $root $ref
                $fetched = $LASTEXITCODE -eq 0
            }
            if (-not $fetched) {
                throw "git fetch failed for $root @ $ref"
            }
            $lines = & git -C $checkout show "FETCH_HEAD:$($Entry.Path)"
            if ($LASTEXITCODE -ne 0) {
                throw "git show failed for $($Entry.Path) in $root @ $ref"
            }
            return (($lines -join "`n") + "`n")
        } finally {
            Remove-Item -LiteralPath $checkout -Recurse -Force -ErrorAction SilentlyContinue
        }
    } else {
        $root = if ($Entry.Repo -eq "webrtc") {
            "https://webrtc.googlesource.com/src"
        } else {
            "https://chromium.googlesource.com/chromium/src"
        }
        $ref = "refs/branch-heads/$branch"
    }
    $url = "$root/+/$ref/$($Entry.Path)`?format=TEXT"
    $encoded = $null
    $lastError = $null
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            $encoded = (Invoke-WebRequest -UseBasicParsing -Uri $url).Content.Trim()
            break
        } catch {
            $lastError = $_.Exception.Message
        }
    }
    if (-not $encoded) {
        throw "Failed to download license source: $url`n$lastError"
    }
    return [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($encoded)).Replace("`r`n", "`n")
}

$builder = [Text.StringBuilder]::new()
[void]$builder.AppendLine("WebRTC m$Milestone third-party license bundle")
[void]$builder.AppendLine()
[void]$builder.AppendLine("This file is generated from the official WebRTC and Chromium branch-heads/$branch source trees.")
[void]$builder.AppendLine("It is a conservative notice set for the desktop static WebRTC components used by Airan-Desk.")
[void]$builder.AppendLine()

foreach ($entry in $entries) {
    if (($entry.Min -and $Milestone -lt $entry.Min) -or
        ($entry.Max -and $Milestone -gt $entry.Max)) {
        continue
    }
    $text = Get-GitilesText -Entry $entry
    [void]$builder.AppendLine(("=" * 79))
    [void]$builder.AppendLine($entry.Name)
    $sourceName = if ($entry.Base) {
        $entry.Base
    } elseif ($entry.Base109 -or $entry.Base144) {
        if ($Milestone -eq 144) { $entry.Base144 } else { $entry.Base109 }
    } else {
        $entry.Repo
    }
    [void]$builder.AppendLine("Source: ${sourceName}:$($entry.Path) @ WebRTC m$Milestone")
    [void]$builder.AppendLine(("-" * 79))
    [void]$builder.Append($text)
    if (-not $text.EndsWith("`n")) {
        [void]$builder.AppendLine()
    }
    [void]$builder.AppendLine()
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
[IO.File]::WriteAllText($OutputPath, $builder.ToString(), [Text.UTF8Encoding]::new($false))
Write-Host "WROTE=$OutputPath"
