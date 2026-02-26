#pragma once

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

struct CameraIntrinsics {
    float fx = 0.0f;
    float fy = 0.0f;
    float cx = 0.0f;
    float cy = 0.0f;
};

inline CameraIntrinsics MakeIntrinsicsFromFovDegrees(float hfovDeg, float vfovDeg, int width, int height) {
    const float hfovRad = hfovDeg * 0.01745329252f;
    const float vfovRad = vfovDeg * 0.01745329252f;
    CameraIntrinsics K;
    K.fx = (width * 0.5f) / std::tan(hfovRad * 0.5f);
    K.fy = (height * 0.5f) / std::tan(vfovRad * 0.5f);
    K.cx = width * 0.5f;
    K.cy = height * 0.5f;
    return K;
}

inline CameraIntrinsics ScaleIntrinsics(const CameraIntrinsics& K, int newW, int newH, int baseW, int baseH) {
    const float sx = static_cast<float>(newW) / static_cast<float>(baseW);
    const float sy = static_cast<float>(newH) / static_cast<float>(baseH);
    CameraIntrinsics out;
    out.fx = K.fx * sx;
    out.fy = K.fy * sy;
    out.cx = K.cx * sx;
    out.cy = K.cy * sy;
    return out;
}

inline void DepthToPointCloud(const float* depth, int width, int height,
                              const CameraIntrinsics& K,
                              std::vector<cv::Vec3f>& outPoints,
                              int stride = 2,
                              float minDepth = 0.1f,
                              float maxDepth = 80.0f) {
    outPoints.clear();
    if (!depth || width <= 0 || height <= 0) return;
    if (stride < 1) stride = 1;

    for (int y = 0; y < height; y += stride) {
        const int row = y * width;
        for (int x = 0; x < width; x += stride) {
            const float z = depth[row + x];
            if (z < minDepth || z > maxDepth) continue;
            const float X = (static_cast<float>(x) - K.cx) * z / K.fx;
            const float Y = (static_cast<float>(y) - K.cy) * z / K.fy;
            outPoints.emplace_back(X, Y, z);
        }
    }
}

inline bool SavePointCloudAsPly(const std::string& path, const std::vector<cv::Vec3f>& points) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out << "ply\nformat ascii 1.0\n";
    out << "element vertex " << points.size() << "\n";
    out << "property float x\nproperty float y\nproperty float z\n";
    out << "end_header\n";
    for (const auto& p : points) {
        out << p[0] << " " << p[1] << " " << p[2] << "\n";
    }
    return true;
}

inline cv::Mat RenderPointCloudView(const float* depth, int width, int height,
                                    const CameraIntrinsics& K,
                                    int outW = 640, int outH = 480,
                                    int stride = 3,
                                    float minDepth = 0.1f,
                                    float maxDepth = 80.0f,
                                    float rotXDeg = -20.0f,
                                    float rotYDeg = 35.0f) {
    cv::Mat canvas(outH, outW, CV_8UC3, cv::Scalar(0, 0, 0));
    if (!depth || width <= 0 || height <= 0) return canvas;
    if (stride < 1) stride = 1;

    std::vector<cv::Vec3f> points;
    DepthToPointCloud(depth, width, height, K, points, stride, minDepth, maxDepth);
    if (points.empty()) return canvas;

    const float rx = rotXDeg * 0.01745329252f;
    const float ry = rotYDeg * 0.01745329252f;
    const float cosY = std::cos(ry);
    const float sinY = std::sin(ry);
    const float cosX = std::cos(rx);
    const float sinX = std::sin(rx);

    float minX = points[0][0], maxX = points[0][0];
    float minY = points[0][1], maxY = points[0][1];
    float minZ = points[0][2], maxZ = points[0][2];

    std::vector<cv::Vec3f> rotated;
    rotated.reserve(points.size());
    for (const auto& p : points) {
        float x = p[0], y = p[1], z = p[2];
        float x1 = x * cosY + z * sinY;
        float z1 = -x * sinY + z * cosY;
        float y1 = y * cosX - z1 * sinX;
        float z2 = y * sinX + z1 * cosX;
        rotated.emplace_back(x1, y1, z2);
        minX = std::min(minX, x1);
        maxX = std::max(maxX, x1);
        minY = std::min(minY, y1);
        maxY = std::max(maxY, y1);
        minZ = std::min(minZ, z2);
        maxZ = std::max(maxZ, z2);
    }

    const float pad = 10.0f;
    const float scaleX = (outW - 2.0f * pad) / (maxX - minX + 1e-6f);
    const float scaleY = (outH - 2.0f * pad) / (maxY - minY + 1e-6f);
    const float scale = std::min(scaleX, scaleY);

    auto depthColor = [&](float z) -> cv::Scalar {
        float t = (z - minZ) / (maxZ - minZ + 1e-6f);
        t = std::max(0.0f, std::min(1.0f, t));
        int r = static_cast<int>(255.0f * std::min(1.0f, std::max(0.0f, 2.0f * (t - 0.5f))));
        int g = static_cast<int>(255.0f * std::min(1.0f, std::max(0.0f, 2.0f * (0.5f - std::abs(t - 0.5f)))));
        int b = static_cast<int>(255.0f * std::min(1.0f, std::max(0.0f, 1.0f - 2.0f * t)));
        return cv::Scalar(b, g, r);
    };

    for (const auto& p : rotated) {
        const int px = static_cast<int>((p[0] - minX) * scale + pad);
        const int py = static_cast<int>(outH - ((p[1] - minY) * scale + pad));
        if (px < 0 || px >= outW || py < 0 || py >= outH) continue;
        const cv::Scalar c = depthColor(p[2]);
        canvas.at<cv::Vec3b>(py, px) = cv::Vec3b(static_cast<uchar>(c[0]),
                                                 static_cast<uchar>(c[1]),
                                                 static_cast<uchar>(c[2]));
    }
    return canvas;
}

inline cv::Mat RenderPointCloudViewRgb(const float* depth, int width, int height,
                                       const CameraIntrinsics& K,
                                       const cv::Mat& bgr,
                                       int outW = 640, int outH = 480,
                                       int stride = 3,
                                       float minDepth = 0.1f,
                                       float maxDepth = 80.0f,
                                       float rotXDeg = -20.0f,
                                       float rotYDeg = 35.0f) {
    cv::Mat canvas(outH, outW, CV_8UC3, cv::Scalar(0, 0, 0));
    if (!depth || width <= 0 || height <= 0) return canvas;
    if (bgr.empty() || bgr.cols != width || bgr.rows != height || bgr.type() != CV_8UC3) {
        return RenderPointCloudView(depth, width, height, K, outW, outH, stride, minDepth, maxDepth, rotXDeg, rotYDeg);
    }
    if (stride < 1) stride = 1;

    const float rx = rotXDeg * 0.01745329252f;
    const float ry = rotYDeg * 0.01745329252f;
    const float cosY = std::cos(ry);
    const float sinY = std::sin(ry);
    const float cosX = std::cos(rx);
    const float sinX = std::sin(rx);

    // First pass: compute bounds after rotation.
    bool first = true;
    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
    for (int y = 0; y < height; y += stride) {
        const int row = y * width;
        for (int x = 0; x < width; x += stride) {
            const float z = depth[row + x];
            if (z < minDepth || z > maxDepth) continue;
            const float X = (static_cast<float>(x) - K.cx) * z / K.fx;
            const float Y = (static_cast<float>(y) - K.cy) * z / K.fy;

            float x1 = X * cosY + z * sinY;
            float z1 = -X * sinY + z * cosY;
            float y1 = Y * cosX - z1 * sinX;
            if (first) {
                minX = maxX = x1;
                minY = maxY = y1;
                first = false;
            } else {
                minX = std::min(minX, x1);
                maxX = std::max(maxX, x1);
                minY = std::min(minY, y1);
                maxY = std::max(maxY, y1);
            }
        }
    }
    if (first) return canvas;

    const float pad = 10.0f;
    const float scaleX = (outW - 2.0f * pad) / (maxX - minX + 1e-6f);
    const float scaleY = (outH - 2.0f * pad) / (maxY - minY + 1e-6f);
    const float scale = std::min(scaleX, scaleY);

    // Second pass: draw.
    for (int y = 0; y < height; y += stride) {
        const int row = y * width;
        for (int x = 0; x < width; x += stride) {
            const float z = depth[row + x];
            if (z < minDepth || z > maxDepth) continue;
            const float X = (static_cast<float>(x) - K.cx) * z / K.fx;
            const float Y = (static_cast<float>(y) - K.cy) * z / K.fy;

            float x1 = X * cosY + z * sinY;
            float z1 = -X * sinY + z * cosY;
            float y1 = Y * cosX - z1 * sinX;

            const int px = static_cast<int>((x1 - minX) * scale + pad);
            const int py = static_cast<int>(outH - ((y1 - minY) * scale + pad));
            if (px < 0 || px >= outW || py < 0 || py >= outH) continue;
            canvas.at<cv::Vec3b>(py, px) = bgr.at<cv::Vec3b>(y, x);
        }
    }
    return canvas;
}
