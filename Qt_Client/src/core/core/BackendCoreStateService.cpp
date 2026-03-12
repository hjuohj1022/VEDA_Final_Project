#include "internal/core/BackendCoreStateService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

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
    if (state->m_latency != ms) {
        state->m_latency = ms;
        emit backend->latencyChanged();
    }
}

