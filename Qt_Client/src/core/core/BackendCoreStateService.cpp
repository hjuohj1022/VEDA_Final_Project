#include "internal/core/BackendCoreStateService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

bool BackendCoreStateService::twoFactorRequired(const BackendPrivate *state) { return state->m_twoFactorRequired; }
bool BackendCoreStateService::twoFactorEnabled(const BackendPrivate *state) { return state->m_twoFactorEnabled; }
QString BackendCoreStateService::userId(const BackendPrivate *state) { return state->m_userId; }
int BackendCoreStateService::sessionRemainingSeconds(const BackendPrivate *state) { return state->m_sessionRemainingSeconds; }
bool BackendCoreStateService::loginLocked(const BackendPrivate *state) { return state->m_loginLocked; }
int BackendCoreStateService::loginFailedAttempts(const BackendPrivate *state) { return state->m_loginFailedAttempts; }
int BackendCoreStateService::loginMaxAttempts(const BackendPrivate *state) { return state->m_loginMaxAttempts; }
int BackendCoreStateService::activeCameras(const BackendPrivate *state) { return state->m_activeCameras; }
int BackendCoreStateService::currentFps(const BackendPrivate *state) { return state->m_currentFps; }
int BackendCoreStateService::latency(const BackendPrivate *state) { return state->m_latency; }
QString BackendCoreStateService::storageUsed(const BackendPrivate *state) { return state->m_storageUsed; }
QString BackendCoreStateService::storageTotal(const BackendPrivate *state) { return state->m_storageTotal; }
int BackendCoreStateService::storagePercent(const BackendPrivate *state) { return state->m_storagePercent; }
int BackendCoreStateService::detectedObjects(const BackendPrivate *state) { return state->m_detectedObjects; }
QString BackendCoreStateService::networkStatus(const BackendPrivate *state) { return state->m_networkStatus; }

QString BackendCoreStateService::thermalFrameDataUrl(const BackendPrivate *state) { return state->m_thermalFrameDataUrl; }
QString BackendCoreStateService::cctv3dMapFrameDataUrl(const BackendPrivate *state) { return state->m_cctv3dMapFrameDataUrl; }
QString BackendCoreStateService::thermalInfoText(const BackendPrivate *state) { return state->m_thermalInfoText; }
bool BackendCoreStateService::thermalStreaming(const BackendPrivate *state) { return state->m_thermalStreaming; }
QString BackendCoreStateService::thermalPalette(const BackendPrivate *state) { return state->m_thermalPalette; }
bool BackendCoreStateService::thermalAutoRange(const BackendPrivate *state) { return state->m_thermalAutoRange; }
int BackendCoreStateService::thermalAutoRangeWindowPercent(const BackendPrivate *state) { return state->m_thermalAutoRangeWindowPercent; }
int BackendCoreStateService::thermalManualMin(const BackendPrivate *state) { return state->m_thermalManualMin; }
int BackendCoreStateService::thermalManualMax(const BackendPrivate *state) { return state->m_thermalManualMax; }
int BackendCoreStateService::displayContrast(const BackendPrivate *state) { return state->m_displayContrast; }
int BackendCoreStateService::displayBrightness(const BackendPrivate *state) { return state->m_displayBrightness; }
int BackendCoreStateService::displaySharpnessLevel(const BackendPrivate *state) { return state->m_displaySharpnessLevel; }
bool BackendCoreStateService::displaySharpnessEnabled(const BackendPrivate *state) { return state->m_displaySharpnessEnabled; }
int BackendCoreStateService::displayColorLevel(const BackendPrivate *state) { return state->m_displayColorLevel; }

void BackendCoreStateService::setActiveCameras(Backend *backend, BackendPrivate *state, int count)
{
    if (state->m_activeCameras != count) {
        state->m_activeCameras = count;
        emit backend->activeCamerasChanged();
    }
}

void BackendCoreStateService::setCurrentFps(Backend *backend, BackendPrivate *state, int fps)
{
    if (state->m_currentFps != fps) {
        state->m_currentFps = fps;
        emit backend->currentFpsChanged();
    }
}

void BackendCoreStateService::setLatency(Backend *backend, BackendPrivate *state, int ms)
{
    // Guard invalid/outlier input range.
    if (ms < 0) {
        ms = 0;
    } else if (ms > 3000) {
        ms = 3000;
    }
    state->m_latencyRaw = ms;

    // Smooth jittery request/connect latency by EMA and damp one-shot spikes.
    constexpr double kAlpha = 0.25;   // New sample weight
    constexpr double kSpikeAbs = 250; // Absolute spike threshold (ms)
    constexpr double kSpikeMul = 2.0; // Relative spike threshold
    constexpr double kSpikeCap = 120; // Max reflected jump for one sample

    if (!state->m_latencyEmaInitialized) {
        state->m_latencyEma = static_cast<double>(ms);
        state->m_latencyEmaInitialized = true;
    } else {
        const double ema = state->m_latencyEma;
        double sample = static_cast<double>(ms);
        if (sample > (ema + kSpikeAbs) && sample > (ema * kSpikeMul)) {
            sample = ema + kSpikeCap;
        }
        state->m_latencyEma = ((1.0 - kAlpha) * ema) + (kAlpha * sample);
    }

    const int smoothed = static_cast<int>(state->m_latencyEma + 0.5);
    if (state->m_latency != smoothed) {
        state->m_latency = smoothed;
        emit backend->latencyChanged();
    }
}

