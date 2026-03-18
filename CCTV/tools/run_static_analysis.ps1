param(
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

Write-Host "[ANALYZE] Configuring build with MSVC /analyze enabled..."
cmake -S . -B build -DENABLE_CLANG_TIDY=OFF -DENABLE_MSVC_ANALYZE=ON | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host "[ANALYZE] Building request_parser_smoke ($Config)..."
cmake --build build --target request_parser_smoke --config $Config --clean-first | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "request_parser_smoke analysis build failed."
}

Write-Host "[ANALYZE] Building depth_trt ($Config)..."
cmake --build build --target depth_trt --config $Config --clean-first | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "depth_trt analysis build failed."
}

Write-Host "[ANALYZE] Static analysis build completed."
