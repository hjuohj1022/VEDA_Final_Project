#include "Backend.h"

// 활성 카메라 수 갱신
void Backend::setActiveCameras(int count) {
    if (m_activeCameras != count) {
        m_activeCameras = count;
        emit activeCamerasChanged();
    }
}

// 현재 FPS 갱신
void Backend::setCurrentFps(int fps) {
    if (m_currentFps != fps) {
        m_currentFps = fps;
        emit currentFpsChanged();
    }
}

// 지연 시간(ms) 갱신
void Backend::setLatency(int ms) {
    if (m_latency != ms) {
        m_latency = ms;
        emit latencyChanged();
    }
}
