#include <fstream>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include "app_config.h"
#include "logging.h"
#include "trt_engine.h"

namespace {
constexpr int kPathSearchDepth = 6;
constexpr std::size_t kRgbChannels = 3U;

class Logger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            LogInfo(std::string("[TRT] ") + msg);
        }
    }
};

Logger& GetLogger() {
    static Logger logger;
    return logger;
}

int SafeDimToInt(int64_t value, int fallback, const char* axisName) {
    if (value < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
        value > static_cast<int64_t>(std::numeric_limits<int>::max())) {
        LogWarn(std::string("[TRT] ") + axisName + " dim out of int range, using fallback=" +
                std::to_string(fallback));
        return fallback;
    }
    return static_cast<int>(value);
}

std::size_t Volume(const nvinfer1::Dims& dims) {
    std::size_t volume = 1U;
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0) {
            return 0U;
        }
        volume *= static_cast<std::size_t>(dims.d[i]);
    }
    return volume;
}

std::string FormatDims(const nvinfer1::Dims& dims) {
    std::ostringstream stream;
    for (int i = 0; i < dims.nbDims; ++i) {
        if (i > 0) {
            stream << 'x';
        }
        stream << dims.d[i];
    }
    return stream.str();
}

bool ReadEngineFile(const std::filesystem::path& enginePath, std::vector<char>& engineData) {
    std::ifstream file(enginePath, std::ios::binary | std::ios::ate);
    if (!file.good()) {
        return false;
    }

    const std::ifstream::pos_type fileSize = file.tellg();
    if (fileSize <= 0) {
        return false;
    }

    engineData.resize(static_cast<std::size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    file.read(engineData.data(), static_cast<std::streamsize>(engineData.size()));
    return file.good();
}

bool AllocateCudaBuffer(void** buffer, const std::size_t bytes, const char* label) {
    if ((buffer == nullptr) || (bytes == 0U)) {
        LogError(std::string("[CUDA] invalid allocation request for ") + label);
        return false;
    }

    if (cudaMalloc(buffer, bytes) != cudaSuccess) {
        LogError(std::string("[CUDA] allocation failed for ") + label + " (" + std::to_string(bytes) + " bytes)");
        return false;
    }
    return true;
}

std::filesystem::path ResolveEnginePath(const std::string& configuredPath) {
    namespace fs = std::filesystem;
    fs::path p(configuredPath);
    if (p.is_absolute()) {
        return p;
    }

    fs::path cwdCandidate = fs::current_path() / p;
    std::error_code ec;
    if (fs::exists(cwdCandidate, ec)) {
        return cwdCandidate;
    }

    fs::path base = fs::current_path();
    for (int i = 0; i < kPathSearchDepth; ++i) {
        fs::path candidate = base / p;
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
        if (!base.has_parent_path()) {
            break;
        }
        base = base.parent_path();
    }
    return p;
}
}  // namespace

CudaResources::~CudaResources() noexcept {
    Reset();
}

void CudaResources::Reset() noexcept {
    if (stream != nullptr) {
        cudaStreamDestroy(stream);
        stream = nullptr;
    }
    if (d_input != nullptr) {
        cudaFree(d_input);
        d_input = nullptr;
    }
    if (d_output != nullptr) {
        cudaFree(d_output);
        d_output = nullptr;
    }
    if (d_raw_img != nullptr) {
        cudaFree(d_raw_img);
        d_raw_img = nullptr;
    }
}

bool InitTrt(TrtContextData& trt) {
    LogInfo("Loading Engine (CUDA Optimized Version)...");
    const std::filesystem::path enginePath = ResolveEnginePath(ENGINE_PATH);
    std::vector<char> engineData;
    if (!ReadEngineFile(enginePath, engineData)) {
        LogError("Cannot read engine file at " + ENGINE_PATH + " (resolved=" + enginePath.string() + ")");
        return false;
    }
    LogInfo("Engine path resolved: " + enginePath.string());

    trt.runtime.reset(nvinfer1::createInferRuntime(GetLogger()));
    if (!trt.runtime) {
        LogError("Failed to create TensorRT runtime.");
        return false;
    }
    trt.engine.reset(trt.runtime->deserializeCudaEngine(engineData.data(), engineData.size()));
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
        const nvinfer1::TensorIOMode mode = trt.engine->getTensorIOMode(name);
        if (mode == nvinfer1::TensorIOMode::kINPUT) {
            trt.inputName = name;
            LogInfo(std::string("[TRT] Found Input Tensor: ") + name);
        } else if (mode == nvinfer1::TensorIOMode::kOUTPUT) {
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
        const nvinfer1::Dims4 fixedInput{1, 3, INPUT_HEIGHT, INPUT_WIDTH};
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

    const nvinfer1::DataType inputType = trt.engine->getTensorDataType(trt.inputName);
    const nvinfer1::DataType outputType = trt.engine->getTensorDataType(trt.outputName);
    if (inputType != nvinfer1::DataType::kFLOAT || outputType != nvinfer1::DataType::kFLOAT) {
        LogError("Expected FP32 tensors. Input type=" + std::to_string(static_cast<int>(inputType)) +
                 " Output type=" + std::to_string(static_cast<int>(outputType)));
        return false;
    }

    trt.inputSize = Volume(trt.inputDims) * sizeof(float);
    trt.outputElements = Volume(trt.outputDims);
    trt.outputSize = trt.outputElements * sizeof(float);
    trt.rawInputSize =
        static_cast<std::size_t>(INPUT_HEIGHT) * static_cast<std::size_t>(INPUT_WIDTH) * kRgbChannels * sizeof(uint8_t);

    if ((trt.inputSize == 0U) || (trt.outputElements == 0U) || (trt.outputSize == 0U) || (trt.rawInputSize == 0U)) {
        LogError("Resolved TensorRT tensor sizes are invalid.");
        return false;
    }

    LogInfo("[TRT] Input Dims: " + FormatDims(trt.inputDims) + " (" + std::to_string(trt.inputSize) + " bytes)");
    LogInfo("[TRT] Output Dims: " + FormatDims(trt.outputDims) + " (" + std::to_string(trt.outputSize) + " bytes)");
    return true;
}

bool InitCudaResources(const TrtContextData& trt, CudaResources& cudaRes) {
    cudaRes.Reset();

    if (!AllocateCudaBuffer(&cudaRes.d_input, trt.inputSize, "input tensor")) {
        cudaRes.Reset();
        return false;
    }
    if (!AllocateCudaBuffer(&cudaRes.d_output, trt.outputSize, "output tensor")) {
        cudaRes.Reset();
        return false;
    }
    if (!AllocateCudaBuffer(&cudaRes.d_raw_img, trt.rawInputSize, "raw input image")) {
        cudaRes.Reset();
        return false;
    }
    if (cudaStreamCreate(&cudaRes.stream) != cudaSuccess) {
        LogError("[CUDA] stream creation failed");
        cudaRes.Reset();
        return false;
    }

    if (!trt.context || !trt.inputName || !trt.outputName) {
        LogError("[TRT] invalid execution context or tensor names");
        cudaRes.Reset();
        return false;
    }
    if (!trt.context->setTensorAddress(trt.inputName, cudaRes.d_input)) {
        LogError("[TRT] failed to bind input tensor address");
        cudaRes.Reset();
        return false;
    }
    if (!trt.context->setTensorAddress(trt.outputName, cudaRes.d_output)) {
        LogError("[TRT] failed to bind output tensor address");
        cudaRes.Reset();
        return false;
    }
    return true;
}

void ResolveOutputHW(const nvinfer1::Dims& outputDims, int& outH, int& outW) {
    outH = INPUT_HEIGHT;
    outW = INPUT_WIDTH;
    if (outputDims.nbDims == 4) {
        outH = SafeDimToInt(outputDims.d[2], INPUT_HEIGHT, "output_h");
        outW = SafeDimToInt(outputDims.d[3], INPUT_WIDTH, "output_w");
    } else if (outputDims.nbDims == 3) {
        outH = SafeDimToInt(outputDims.d[1], INPUT_HEIGHT, "output_h");
        outW = SafeDimToInt(outputDims.d[2], INPUT_WIDTH, "output_w");
    } else if (outputDims.nbDims == 2) {
        outH = SafeDimToInt(outputDims.d[0], INPUT_HEIGHT, "output_h");
        outW = SafeDimToInt(outputDims.d[1], INPUT_WIDTH, "output_w");
    }
}
