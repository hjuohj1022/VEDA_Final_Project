#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include "app_config.h"
#include "logging.h"
#include "trt_engine.h"

using namespace nvinfer1;

namespace {
class Logger : public ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) LogInfo(std::string("[TRT] ") + msg);
    }
};

Logger& GetLogger() {
    static Logger logger;
    return logger;
}

size_t Volume(const Dims& dims) {
    size_t v = 1;
    for (int i = 0; i < dims.nbDims; ++i) v *= static_cast<size_t>(dims.d[i]);
    return v;
}

std::filesystem::path ResolveEnginePath(const std::string& configuredPath) {
    namespace fs = std::filesystem;
    fs::path p(configuredPath);
    if (p.is_absolute()) return p;

    // 1) current working directory 기준
    fs::path cwdCandidate = fs::current_path() / p;
    std::error_code ec;
    if (fs::exists(cwdCandidate, ec)) return cwdCandidate;

    // 2) 상위 디렉터리를 순회하며 프로젝트 루트 상대경로 후보 탐색
    //    (예: build/Release 에서 실행 시 ../../ml_assets/...)
    fs::path base = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = base / p;
        if (fs::exists(candidate, ec)) return candidate;
        if (!base.has_parent_path()) break;
        base = base.parent_path();
    }

    // fallback: 원본 상대경로 반환
    return p;
}
}  // namespace

CudaResources::~CudaResources() {
    if (stream) cudaStreamDestroy(stream);
    if (d_input) cudaFree(d_input);
    if (d_output) cudaFree(d_output);
    if (d_raw_img) cudaFree(d_raw_img);
}

bool InitTrt(TrtContextData& trt) {
    LogInfo("Loading Engine (CUDA Optimized Version)...");
    const std::filesystem::path enginePath = ResolveEnginePath(ENGINE_PATH);
    std::ifstream file(enginePath, std::ios::binary | std::ios::ate);
    if (!file.good()) {
        LogError("Cannot find engine file at " + ENGINE_PATH +
                 " (resolved=" + enginePath.string() + ")");
        return false;
    }
    LogInfo("Engine path resolved: " + enginePath.string());
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<char> engineData(size);
    file.read(engineData.data(), static_cast<std::streamsize>(size));
    file.close();

    trt.runtime.reset(createInferRuntime(GetLogger()));
    if (!trt.runtime) {
        LogError("Failed to create TensorRT runtime.");
        return false;
    }
    trt.engine.reset(trt.runtime->deserializeCudaEngine(engineData.data(), size));
    if (!trt.engine) {
        LogError("Failed to deserialize TensorRT engine.");
        return false;
    }
    trt.context.reset(trt.engine->createExecutionContext());
    if (!trt.context) {
        LogError("Failed to create TensorRT execution context.");
        return false;
    }

    for (int i = 0; i < trt.engine->getNbIOTensors(); ++i) {
        const char* name = trt.engine->getIOTensorName(i);
        TensorIOMode mode = trt.engine->getTensorIOMode(name);
        if (mode == TensorIOMode::kINPUT) {
            trt.inputName = name;
            LogInfo(std::string("[TRT] Found Input Tensor: ") + name);
        } else if (mode == TensorIOMode::kOUTPUT) {
            trt.outputName = name;
            LogInfo(std::string("[TRT] Found Output Tensor: ") + name);
        }
    }

    if (!trt.inputName || !trt.outputName) {
        LogError("Failed to find input/output tensor names.");
        return false;
    }

    trt.inputDims = trt.context->getTensorShape(trt.inputName);
    bool needSetInput = false;
    for (int i = 0; i < trt.inputDims.nbDims; ++i) {
        if (trt.inputDims.d[i] < 0) {
            needSetInput = true;
            break;
        }
    }
    if (needSetInput || trt.inputDims.nbDims == 0) {
        Dims4 fixedInput{1, 3, INPUT_HEIGHT, INPUT_WIDTH};
        if (!trt.context->setInputShape(trt.inputName, fixedInput)) {
            LogError("Failed to set input shape.");
            return false;
        }
    }

    trt.inputDims = trt.context->getTensorShape(trt.inputName);
    trt.outputDims = trt.context->getTensorShape(trt.outputName);

    for (int i = 0; i < trt.inputDims.nbDims; ++i) {
        if (trt.inputDims.d[i] < 0) {
            LogError("Input dims unresolved after setInputShape.");
            return false;
        }
    }
    for (int i = 0; i < trt.outputDims.nbDims; ++i) {
        if (trt.outputDims.d[i] < 0) {
            LogError("Output dims unresolved.");
            return false;
        }
    }

    nvinfer1::DataType inputType = trt.engine->getTensorDataType(trt.inputName);
    nvinfer1::DataType outputType = trt.engine->getTensorDataType(trt.outputName);
    if (inputType != nvinfer1::DataType::kFLOAT || outputType != nvinfer1::DataType::kFLOAT) {
        LogError("Expected FP32 tensors. Input type=" + std::to_string(static_cast<int>(inputType)) +
                 " Output type=" + std::to_string(static_cast<int>(outputType)));
        return false;
    }

    trt.inputSize = Volume(trt.inputDims) * sizeof(float);
    trt.outputElements = Volume(trt.outputDims);
    trt.outputSize = trt.outputElements * sizeof(float);
    trt.rawInputSize = static_cast<size_t>(INPUT_HEIGHT) * static_cast<size_t>(INPUT_WIDTH) * 3 * sizeof(uint8_t);

    std::cout << "[TRT] Input Dims: ";
    for (int i = 0; i < trt.inputDims.nbDims; ++i) std::cout << trt.inputDims.d[i] << (i + 1 < trt.inputDims.nbDims ? "x" : "");
    std::cout << " (" << trt.inputSize << " bytes)" << std::endl;
    std::cout << "[TRT] Output Dims: ";
    for (int i = 0; i < trt.outputDims.nbDims; ++i) std::cout << trt.outputDims.d[i] << (i + 1 < trt.outputDims.nbDims ? "x" : "");
    std::cout << " (" << trt.outputSize << " bytes)" << std::endl;
    return true;
}

bool InitCudaResources(const TrtContextData& trt, CudaResources& cudaRes) {
    if (cudaMalloc(&cudaRes.d_input, trt.inputSize) != cudaSuccess) return false;
    if (cudaMalloc(&cudaRes.d_output, trt.outputSize) != cudaSuccess) return false;
    if (cudaMalloc(&cudaRes.d_raw_img, trt.rawInputSize) != cudaSuccess) return false;
    if (cudaStreamCreate(&cudaRes.stream) != cudaSuccess) return false;

    if (!trt.context->setTensorAddress(trt.inputName, cudaRes.d_input)) return false;
    if (!trt.context->setTensorAddress(trt.outputName, cudaRes.d_output)) return false;
    return true;
}

void ResolveOutputHW(const Dims& outputDims, int& outH, int& outW) {
    outH = INPUT_HEIGHT;
    outW = INPUT_WIDTH;
    if (outputDims.nbDims == 4) {
        outH = outputDims.d[2];
        outW = outputDims.d[3];
    } else if (outputDims.nbDims == 3) {
        outH = outputDims.d[1];
        outW = outputDims.d[2];
    } else if (outputDims.nbDims == 2) {
        outH = outputDims.d[0];
        outW = outputDims.d[1];
    }
}
