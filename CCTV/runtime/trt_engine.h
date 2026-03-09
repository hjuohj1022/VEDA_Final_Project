#pragma once

#include <cstddef>
#include <memory>

#include <NvInfer.h>
#include <cuda_runtime_api.h>

struct TrtDeleter {
    template <typename T>
    void operator()(T* obj) const noexcept {
        delete obj;
    }
};

using TrtRuntime = std::unique_ptr<nvinfer1::IRuntime, TrtDeleter>;
using TrtEngine = std::unique_ptr<nvinfer1::ICudaEngine, TrtDeleter>;
using TrtContextPtr = std::unique_ptr<nvinfer1::IExecutionContext, TrtDeleter>;

struct TrtContextData {
    TrtRuntime runtime;
    TrtEngine engine;
    TrtContextPtr context;
    const char* inputName = nullptr;
    const char* outputName = nullptr;
    nvinfer1::Dims inputDims{};
    nvinfer1::Dims outputDims{};
    std::size_t inputSize = 0U;
    std::size_t outputElements = 0U;
    std::size_t outputSize = 0U;
    std::size_t rawInputSize = 0U;
};

struct CudaResources {
    void* d_input = nullptr;
    void* d_output = nullptr;
    void* d_raw_img = nullptr;
    cudaStream_t stream = nullptr;

    ~CudaResources() noexcept;
    void Reset() noexcept;
};

bool InitTrt(TrtContextData& trt);
bool InitCudaResources(const TrtContextData& trt, CudaResources& cudaRes);
void ResolveOutputHW(const nvinfer1::Dims& outputDims, int& outH, int& outW);
