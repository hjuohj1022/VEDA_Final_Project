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

$scriptPath = Join-Path $PSScriptRoot "tools/bootstrap/setup_dependencies.ps1"
& $scriptPath `
    -ProjectRoot $ProjectRoot `
    -OpenCvUrl $OpenCvUrl `
    -TensorRtUrl $TensorRtUrl `
    -Da3MetricRepo $Da3MetricRepo `
    -Da3MetricTargetDir $Da3MetricTargetDir `
    -CudaPath $CudaPath `
    -SkipOpenCV:$SkipOpenCV `
    -SkipTensorRT:$SkipTensorRT `
    -SkipDa3Metric:$SkipDa3Metric `
    -Force:$Force
exit $LASTEXITCODE
