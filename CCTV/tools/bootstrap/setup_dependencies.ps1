param(
    [string]$ProjectRoot = "",
    [string]$OpenCvUrl = "https://github.com/opencv/opencv/releases/download/4.10.0/opencv-4.10.0-windows.exe",
    [string]$TensorRtUrl = "",
    [string]$Da3MetricRepo = "depth-anything/da3metric-large",
    [string]$Da3MetricTargetDir = "ml_assets/checkpoints/DA3METRIC-LARGE",
    [string]$CudaPath = "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.1",
    [switch]$SkipOpenCV,
    [switch]$SkipTensorRT,
    [switch]$SkipDa3Metric,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step([string]$Message) {
    Write-Host "[STEP] $Message" -ForegroundColor Cyan
}

function Write-WarnMsg([string]$Message) {
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Info([string]$Message) {
    Write-Host "[INFO] $Message" -ForegroundColor Green
}

function Ensure-Dir([string]$Path) {
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Download-File([string]$Url, [string]$OutFile, [switch]$Overwrite) {
    if ((Test-Path $OutFile) -and (-not $Overwrite)) {
        Write-Info "Already exists: $OutFile"
        return
    }
    Write-Step "Downloading: $Url"
    Invoke-WebRequest -Uri $Url -OutFile $OutFile
}

function Try-Download-File([string]$Url, [string]$OutFile, [switch]$Overwrite) {
    try {
        Download-File -Url $Url -OutFile $OutFile -Overwrite:$Overwrite
        return $true
    } catch {
        Write-WarnMsg "Download failed: $Url"
        if (Test-Path $OutFile) {
            Remove-Item -Force $OutFile -ErrorAction SilentlyContinue
        }
        return $false
    }
}

function Expand-Zip([string]$ZipPath, [string]$DestDir, [switch]$Overwrite) {
    if ((Test-Path $DestDir) -and (-not $Overwrite)) {
        Write-Info "Already extracted: $DestDir"
        return
    }
    if ((Test-Path $DestDir) -and $Overwrite) {
        Remove-Item -Recurse -Force $DestDir
    }
    Write-Step "Extracting ZIP: $ZipPath"
    Expand-Archive -Path $ZipPath -DestinationPath $DestDir -Force
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    # script path: <project>/tools/bootstrap/setup_dependencies.ps1
    $ProjectRoot = Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\\..")
}
$ProjectRoot = (Resolve-Path $ProjectRoot).Path

$DownloadsDir = Join-Path $ProjectRoot "_downloads"
Ensure-Dir $DownloadsDir

Write-Info "ProjectRoot: $ProjectRoot"

if (-not $SkipOpenCV) {
    $opencvExe = Join-Path $DownloadsDir "opencv-4.10.0-windows.exe"
    $opencvRoot = Join-Path $ProjectRoot "opencv"

    Download-File -Url $OpenCvUrl -OutFile $opencvExe -Overwrite:$Force

    if ((Test-Path $opencvRoot) -and (-not $Force)) {
        Write-Info "OpenCV already exists: $opencvRoot"
    } else {
        Write-Step "Extracting OpenCV self-extractor"
        if ((Test-Path $opencvRoot) -and $Force) {
            Remove-Item -Recurse -Force $opencvRoot
        }
        # OpenCV official exe supports: -o<dir> -y
        & $opencvExe "-o$ProjectRoot" "-y" | Out-Null
        Write-Info "OpenCV extracted under: $opencvRoot"
    }
}

if (-not $SkipTensorRT) {
    $resolvedTensorRtUrl = $TensorRtUrl
    $trtZip = Join-Path $DownloadsDir "tensorrt_windows.zip"
    if ([string]::IsNullOrWhiteSpace($resolvedTensorRtUrl)) {
        $cudaTag = "12.1"
        if ($CudaPath -match "v(\d+\.\d+)") {
            $cudaTag = $Matches[1]
        }
        $base = "https://developer.nvidia.com/downloads/compute/machine-learning/tensorrt/10.15.1/zip"
        $cudaMatchedUrl = "$base/TensorRT-10.15.1.29.Windows.win10.cuda-$cudaTag.zip"
        $fallbackUrl = "$base/TensorRT-10.15.1.29.Windows.win10.cuda-12.9.zip"
        $candidates = @($cudaMatchedUrl)
        if ($fallbackUrl -ne $cudaMatchedUrl) {
            $candidates += $fallbackUrl
        }
        Write-Info "TensorRT URL not provided. Auto-selecting by CUDA path: $CudaPath"

        $resolved = $false
        foreach ($candidate in $candidates) {
            if (Try-Download-File -Url $candidate -OutFile $trtZip -Overwrite:$Force) {
                $resolvedTensorRtUrl = $candidate
                $resolved = $true
                break
            }
        }
        if (-not $resolved) {
            throw "Failed to download TensorRT archive from all candidates. Use -TensorRtUrl '<direct zip url>'."
        }
        Write-Info "Using TensorRT archive: $resolvedTensorRtUrl"
    } else {
        Download-File -Url $resolvedTensorRtUrl -OutFile $trtZip -Overwrite:$Force
        Write-Info "Using TensorRT archive: $resolvedTensorRtUrl"
    }

    $trtExtractBase = Join-Path $ProjectRoot "_tensorrt_extract"
    Expand-Zip -ZipPath $trtZip -DestDir $trtExtractBase -Overwrite:$Force

    $trtCandidate = Get-ChildItem -Path $trtExtractBase -Directory -Filter "TensorRT*" | Select-Object -First 1
    if (-not $trtCandidate) {
        throw "TensorRT folder not found in archive."
    }

    $targetTrt = Join-Path $ProjectRoot $trtCandidate.Name
    if ((Test-Path $targetTrt) -and $Force) {
        Remove-Item -Recurse -Force $targetTrt
    }
    if (-not (Test-Path $targetTrt)) {
        Move-Item -Path $trtCandidate.FullName -Destination $targetTrt
    }
    Write-Info "TensorRT prepared at: $targetTrt"
}

if (-not $SkipDa3Metric) {
    $da3Dir = Join-Path $ProjectRoot $Da3MetricTargetDir
    Ensure-Dir $da3Dir
    if ((Test-Path $da3Dir) -and $Force) {
        Remove-Item -Recurse -Force $da3Dir
        Ensure-Dir $da3Dir
    }

    $hfBase = "https://huggingface.co/$Da3MetricRepo/resolve/main"
    $da3Files = @(
        "config.json",
        "model.safetensors",
        "README.md",
        ".gitattributes"
    )

    foreach ($f in $da3Files) {
        $url = "$hfBase/${f}?download=true"
        $outFile = Join-Path $da3Dir $f
        Download-File -Url $url -OutFile $outFile -Overwrite:$Force
    }
    Write-Info "DA3 Metric checkpoint prepared at: $da3Dir"
}

$opencvPath = "opencv/build"
$trtDir = Get-ChildItem -Path $ProjectRoot -Directory -Filter "TensorRT*" | Select-Object -First 1
if (-not $trtDir) {
    $trtPath = "TensorRT-10.x.x.x"
    Write-WarnMsg "TensorRT folder not found; local_paths.cmake will use placeholder."
} else {
    $trtPath = $trtDir.Name
}

$localPaths = @"
# TensorRT path
set(TRT_PATH "$trtPath")

# OpenCV path
set(OPENCV_PATH "$opencvPath")

# CUDA path
set(CUDA_PATH "$CudaPath")
"@

$localPathsFile = Join-Path $ProjectRoot "config/local_paths.cmake"
Set-Content -Path $localPathsFile -Value $localPaths -Encoding ASCII
Write-Info "Generated: $localPathsFile"

Write-Host ""
Write-Host "Done. Next steps:" -ForegroundColor Cyan
Write-Host "1) Verify CUDA Toolkit is installed at: $CudaPath"
Write-Host "2) Verify config/app_config.h ENGINE_PATH exists on this PC"
Write-Host "3) (Optional) Export ONNX from DA3 Metric: python tools/export/export_da3metric_onnx.py --height 560 --width 1008"
Write-Host "4) Build: cmake -S . -B build ; cmake --build build --config Release"
