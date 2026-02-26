#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <chrono>
#include <thread>
#include <atomic>

// TensorRT & CUDA
#include <NvInfer.h>
#include <cuda_runtime_api.h>

// Windows networking
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// OpenCV
#include <opencv2/opencv.hpp>

using namespace nvinfer1;
using namespace cv;

// 설정 파일 포함 (.gitignore로 관리됨)
#include "app_config.h"
#include "logging.h"
#include "request.h"
#include "runner.h"
#include "server.h"

static bool CheckCuda(cudaError_t result, char const* const func, const char* const file, int const line) {
    if (result != cudaSuccess) {
        LogError("CUDA error at " + std::string(file) + ":" + std::to_string(line) +
                 " code=" + std::to_string(static_cast<int>(result)) + " \"" + func + "\"");
        return false;
    }
    return true;
}

#define CHECK_CUDA_OR_RETURN(val)                                      \
    do {                                                               \
        if (!CheckCuda((val), #val, __FILE__, __LINE__)) return false; \
    } while (0)

// ==========================================
// [CUDA 커널] 전처리 (Resize + Normalize + CHW)
// OpenCV CPU Resize는 느리므로, 원본 그대로 GPU로 올려서 처리하는게 가장 빠름.
// 하지만 구현 복잡도를 낮추기 위해 CPU에서 Resize 후 GPU로 올린 뒤
// "Normalization + HWC->CHW" 변환을 GPU 커널로 처리.
// ==========================================
__global__ void preprocess_kernel(const uint8_t* src, float* dst, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = y * width + x;
    
    // BGR -> RGB 순서로 접근 및 Normalize
    // src는 HWC(BGR), dst는 CHW(RGB)
    
    float b = src[idx * 3 + 0] / 255.0f;
    float g = src[idx * 3 + 1] / 255.0f;
    float r = src[idx * 3 + 2] / 255.0f;

    // (val - mean) / std (DepthAnything V2 Standard)
    r = (r - 0.485f) / 0.229f;
    g = (g - 0.456f) / 0.224f;
    b = (b - 0.406f) / 0.225f;

    // Plane offset (Channel size)
    int plane = width * height;

    // CHW 저장 (RGB 순서)
    dst[idx]             = r; // 0번 채널
    dst[idx + plane]     = g; // 1번 채널
    dst[idx + plane * 2] = b; // 2번 채널
}

class Logger : public ILogger {
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) LogInfo(std::string("[TRT] ") + msg);
    }
} gLogger;

void LogInfo(const std::string& msg) {
    std::cout << "[INFO] " << msg << std::endl;
}

void LogWarn(const std::string& msg) {
    std::cout << "[WARN] " << msg << std::endl;
}

void LogError(const std::string& msg) {
    std::cerr << "[ERR] " << msg << std::endl;
}

struct TrtDeleter {
    template <typename T>
    void operator()(T* obj) const noexcept {
        delete obj;
    }
};

using TrtRuntime = std::unique_ptr<IRuntime, TrtDeleter>;
using TrtEngine = std::unique_ptr<ICudaEngine, TrtDeleter>;
using TrtContextPtr = std::unique_ptr<IExecutionContext, TrtDeleter>;

std::vector<std::string> SplitTokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

bool ParseInt(const std::string& s, int& out) {
    char* end = nullptr;
    long val = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = static_cast<int>(val);
    return true;
}

Request ParseRequest(const std::string& line) {
    Request req;
    auto tokens = SplitTokens(line);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t == "stop") {
            req.stop = true;
            continue;
        }
        if (t == "headless" || t == "headless=1" || t == "headless=true") {
            req.headless = true;
            req.headlessSet = true;
            continue;
        }
        if (t == "headless=0" || t == "headless=false") {
            req.headless = false;
            req.headlessSet = true;
            continue;
        }
        if (t == "gui" || t == "gui=1" || t == "gui=true") {
            req.gui = true;
            req.headlessSet = true;
            req.headless = false;
            continue;
        }
        if (t.rfind("channel=", 0) == 0) {
            int ch = -1;
            if (ParseInt(t.substr(8), ch)) req.channel = ch;
            continue;
        }
        if (t == "channel" && i + 1 < tokens.size()) {
            int ch = -1;
            if (ParseInt(tokens[i + 1], ch)) req.channel = ch;
            ++i;
            continue;
        }
        int ch = -1;
        if (ParseInt(t, ch)) {
            req.channel = ch;
            continue;
        }
    }
    return req;
}

void SendResponse(SOCKET client, const std::string& msg) {
    send(client, msg.c_str(), static_cast<int>(msg.size()), 0);
}

struct TrtContextData {
    TrtRuntime runtime;
    TrtEngine engine;
    TrtContextPtr context;
    const char* inputName = nullptr;
    const char* outputName = nullptr;
    Dims inputDims{};
    Dims outputDims{};
    size_t inputSize = 0;
    size_t outputElements = 0;
    size_t outputSize = 0;
    size_t rawInputSize = 0;
};

struct CudaResources {
    void* d_input = nullptr;
    void* d_output = nullptr;
    void* d_raw_img = nullptr;
    cudaStream_t stream = nullptr;

    ~CudaResources() {
        if (stream) cudaStreamDestroy(stream);
        if (d_input) cudaFree(d_input);
        if (d_output) cudaFree(d_output);
        if (d_raw_img) cudaFree(d_raw_img);
    }
};

static size_t Volume(const Dims& dims) {
    size_t v = 1;
    for (int i = 0; i < dims.nbDims; ++i) v *= static_cast<size_t>(dims.d[i]);
    return v;
}

static bool InitTrt(TrtContextData& trt) {
    LogInfo("Loading Engine (CUDA Optimized Version)...");
    std::ifstream file(ENGINE_PATH, std::ios::binary | std::ios::ate);
    if (!file.good()) {
        LogError("Cannot find engine file at " + ENGINE_PATH);
        return false;
    }
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    trt.runtime.reset(createInferRuntime(gLogger));
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
        Dims4 fixedInput{1, 3, INPUT_SIZE, INPUT_SIZE};
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
    trt.rawInputSize = INPUT_SIZE * INPUT_SIZE * 3 * sizeof(uint8_t);

    std::cout << "[TRT] Input Dims: ";
    for (int i = 0; i < trt.inputDims.nbDims; ++i) std::cout << trt.inputDims.d[i] << (i + 1 < trt.inputDims.nbDims ? "x" : "");
    std::cout << " (" << trt.inputSize << " bytes)" << std::endl;
    std::cout << "[TRT] Output Dims: ";
    for (int i = 0; i < trt.outputDims.nbDims; ++i) std::cout << trt.outputDims.d[i] << (i + 1 < trt.outputDims.nbDims ? "x" : "");
    std::cout << " (" << trt.outputSize << " bytes)" << std::endl;

    return true;
}

static bool InitCudaResources(const TrtContextData& trt, CudaResources& cudaRes) {
    CHECK_CUDA_OR_RETURN(cudaMalloc(&cudaRes.d_input, trt.inputSize));
    CHECK_CUDA_OR_RETURN(cudaMalloc(&cudaRes.d_output, trt.outputSize));
    CHECK_CUDA_OR_RETURN(cudaMalloc(&cudaRes.d_raw_img, trt.rawInputSize));
    CHECK_CUDA_OR_RETURN(cudaStreamCreate(&cudaRes.stream));

    if (!trt.context->setTensorAddress(trt.inputName, cudaRes.d_input)) return false;
    if (!trt.context->setTensorAddress(trt.outputName, cudaRes.d_output)) return false;
    return true;
}

static void ResolveOutputHW(const Dims& outputDims, int& outH, int& outW) {
    outH = INPUT_SIZE;
    outW = INPUT_SIZE;
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

bool RunDepthWorker(int channel, bool headless, std::atomic<bool>& stopFlag) {
    LogInfo("Mode: " + std::string(headless ? "headless" : "gui") +
            " | Channel: " + std::to_string(channel));

    TrtContextData trt;
    if (!InitTrt(trt)) return false;

    CudaResources cudaRes;
    if (!InitCudaResources(trt, cudaRes)) return false;

    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp|stimeout;5000000|rw_timeout;5000000");
    const std::string& rtspUrl = RTSP_URLS[channel];
    VideoCapture cap(rtspUrl, CAP_FFMPEG);
    cap.set(CAP_PROP_BUFFERSIZE, 1);
    cap.set(CAP_PROP_OPEN_TIMEOUT_MSEC, 3000);
    cap.set(CAP_PROP_READ_TIMEOUT_MSEC, 3000);
    if (!cap.isOpened()) {
        LogError("Cannot open RTSP stream: " + rtspUrl);
        return false;
    }

    Mat frame, resized;
    std::vector<float> outputBuffer(trt.outputElements);
    uint64_t frameIdx = 0;

    LogInfo("Stream started.");

    dim3 block(16, 16);
    dim3 grid((INPUT_SIZE + block.x - 1) / block.x, (INPUT_SIZE + block.y - 1) / block.y);

    auto prevTime = std::chrono::high_resolution_clock::now();

    int grabFailCount = 0;
    const int grabFailLogEvery = 30;
    bool fatalError = false;
    while (!stopFlag.load()) {
        if (!cap.grab()) {
            grabFailCount++;
            if (grabFailCount % grabFailLogEvery == 1) {
                LogWarn("RTSP grab failed (" + std::to_string(grabFailCount) +
                        "x). url=" + rtspUrl);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!cap.retrieve(frame)) continue;
        grabFailCount = 0;
        frameIdx++;
        if (frame.empty()) {
            LogWarn("Empty frame at idx=" + std::to_string(frameIdx));
            continue;
        }
        if (frame.channels() != 3) {
            LogWarn("Unexpected channels=" + std::to_string(frame.channels()) +
                    " at idx=" + std::to_string(frameIdx));
            continue;
        }

        cv::resize(frame, resized, Size(INPUT_SIZE, INPUT_SIZE), 0, 0, INTER_LINEAR);
        if (resized.empty() || resized.cols != INPUT_SIZE || resized.rows != INPUT_SIZE) {
            LogWarn("Resize failed or size mismatch at idx=" + std::to_string(frameIdx));
            continue;
        }
        if (resized.type() != CV_8UC3) {
            LogWarn("Unexpected resized type=" + std::to_string(resized.type()) +
                    " at idx=" + std::to_string(frameIdx));
            continue;
        }

        if (!resized.isContinuous()) {
            resized = resized.clone();
        }
        if (!CheckCuda(cudaMemcpyAsync(cudaRes.d_raw_img, resized.data, trt.rawInputSize,
                                        cudaMemcpyHostToDevice, cudaRes.stream),
                       "cudaMemcpyAsync(d_raw_img)", __FILE__, __LINE__)) {
            fatalError = true;
            break;
        }

        preprocess_kernel<<<grid, block, 0, cudaRes.stream>>>((uint8_t*)cudaRes.d_raw_img,
                                                               (float*)cudaRes.d_input,
                                                               INPUT_SIZE, INPUT_SIZE);
        auto err = cudaGetLastError();
        if (err != cudaSuccess) {
            LogError(std::string("CUDA kernel error: ") + cudaGetErrorString(err) +
                     " (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            break;
        }

        if (!trt.context->enqueueV3(cudaRes.stream)) {
            LogError("enqueueV3 failed (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            break;
        }
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            LogError(std::string("CUDA post-enqueue error: ") + cudaGetErrorString(err) +
                     " (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            break;
        }

        if (!CheckCuda(cudaMemcpyAsync(outputBuffer.data(), cudaRes.d_output, trt.outputSize,
                                        cudaMemcpyDeviceToHost, cudaRes.stream),
                       "cudaMemcpyAsync(output)", __FILE__, __LINE__)) {
            fatalError = true;
            break;
        }
        err = cudaStreamSynchronize(cudaRes.stream);
        if (err != cudaSuccess) {
            LogError(std::string("CUDA stream sync error: ") + cudaGetErrorString(err) +
                     " (frame=" + std::to_string(frameIdx) + ")");
            fatalError = true;
            break;
        }

        int outH = INPUT_SIZE;
        int outW = INPUT_SIZE;
        ResolveOutputHW(trt.outputDims, outH, outW);
        if (static_cast<size_t>(outH) * static_cast<size_t>(outW) > trt.outputElements) {
            LogWarn("Output size mismatch outH*outW=" +
                    std::to_string(static_cast<size_t>(outH) * static_cast<size_t>(outW)) +
                    " > outputElements=" + std::to_string(trt.outputElements) +
                    " (frame=" + std::to_string(frameIdx) + ")");
            continue;
        }
        Mat depthMat(outH, outW, CV_32F, outputBuffer.data());
        double minVal, maxVal;
        cv::minMaxLoc(depthMat, &minVal, &maxVal);

        Mat depthNorm;
        depthMat.convertTo(depthNorm, CV_8U, 255.0 / (maxVal - minVal + 1e-5),
                           -minVal * 255.0 / (maxVal - minVal + 1e-5));

        Mat depthColor;
        cv::applyColorMap(depthNorm, depthColor, COLORMAP_INFERNO);

        Mat showFrame, showDepth;
        cv::resize(frame, showFrame, Size(640, 480));
        cv::resize(depthColor, showDepth, Size(640, 480));

        if (!headless) {
            Mat combined;
            cv::hconcat(showFrame, showDepth, combined);

            auto currTime = std::chrono::high_resolution_clock::now();
            double fps = 1.0 / std::chrono::duration<double>(currTime - prevTime).count();
            prevTime = currTime;

            cv::putText(combined, "C++ CUDA FPS: " + std::to_string((int)fps),
                        Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 0), 2);
            cv::imshow("Depth Anything V2 (CUDA Optimized)", combined);
            cv::waitKey(1);
        }
    }

    cap.release();
    if (!headless) {
        cv::destroyAllWindows();
    }

    if (fatalError) return false;
    return true;
}

int main(int argc, char** argv) {
    int port = 9090;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--port=", 0) == 0) {
            port = std::stoi(arg.substr(7));
        }
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "socket failed" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, 5) == SOCKET_ERROR) {
        std::cerr << "listen failed" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    LogInfo("Listening on port " + std::to_string(port));

    std::thread worker;
    std::atomic<bool> workerStop{false};
    bool workerRunning = false;

    while (true) {
        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept(server, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (client == INVALID_SOCKET) continue;

        char buf[1024];
        int len = recv(client, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            LogWarn("Client connected but sent no data");
            closesocket(client);
            continue;
        }
        buf[len] = '\0';

        std::string line(buf);
        {
            char ip[64] = {0};
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            LogInfo(std::string("Request from ") + ip + ":" +
                    std::to_string(ntohs(clientAddr.sin_port)) + " -> " + line);
        }
        Request req = ParseRequest(line);

        if (req.channel < -1 || req.channel > 3) {
            LogWarn("Invalid channel request");
            SendResponse(client, "ERR invalid channel\n");
            closesocket(client);
            continue;
        }

        if (workerRunning) {
            workerStop.store(true);
            if (worker.joinable()) worker.join();
            workerRunning = false;
        }

        if (req.stop) {
            LogInfo("Stop request processed");
            SendResponse(client, "OK stopped\n");
            closesocket(client);
            continue;
        }

        int channel = req.channel >= 0 ? req.channel : RTSP_CHANNEL;
        bool headless = req.headlessSet ? req.headless : false;
        if (req.gui) headless = false;

        workerStop.store(false);
        worker = std::thread([channel, headless, &workerStop]() {
            bool ok = RunDepthWorker(channel, headless, workerStop);
            if (!ok) LogError("Worker exited with errors.");
        });
        workerRunning = true;

        std::string modeStr = headless ? "headless" : "gui";
        SendResponse(client, "OK started channel=" + std::to_string(channel) + " mode=" + modeStr + "\n");
        closesocket(client);
    }

    if (workerRunning) {
        workerStop.store(true);
        if (worker.joinable()) worker.join();
    }
    closesocket(server);
    WSACleanup();
    return 0;
}
