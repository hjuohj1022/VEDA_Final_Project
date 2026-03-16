#include "Backend.h"
#include "internal/core/BackendCoreStateService.h"
#include "internal/core/Backend_p.h"

bool Backend::twoFactorRequired() const { return BackendCoreStateService::twoFactorRequired(d_ptr.get()); }
bool Backend::twoFactorEnabled() const { return BackendCoreStateService::twoFactorEnabled(d_ptr.get()); }
QString Backend::userId() const { return BackendCoreStateService::userId(d_ptr.get()); }
int Backend::sessionRemainingSeconds() const { return BackendCoreStateService::sessionRemainingSeconds(d_ptr.get()); }
bool Backend::loginLocked() const { return BackendCoreStateService::loginLocked(d_ptr.get()); }
int Backend::loginFailedAttempts() const { return BackendCoreStateService::loginFailedAttempts(d_ptr.get()); }
int Backend::loginMaxAttempts() const { return BackendCoreStateService::loginMaxAttempts(d_ptr.get()); }
int Backend::activeCameras() const { return BackendCoreStateService::activeCameras(d_ptr.get()); }
int Backend::currentFps() const { return BackendCoreStateService::currentFps(d_ptr.get()); }
int Backend::latency() const { return BackendCoreStateService::latency(d_ptr.get()); }
QString Backend::storageUsed() const { return BackendCoreStateService::storageUsed(d_ptr.get()); }
QString Backend::storageTotal() const { return BackendCoreStateService::storageTotal(d_ptr.get()); }
int Backend::storagePercent() const { return BackendCoreStateService::storagePercent(d_ptr.get()); }
int Backend::detectedObjects() const { return BackendCoreStateService::detectedObjects(d_ptr.get()); }
QString Backend::networkStatus() const { return BackendCoreStateService::networkStatus(d_ptr.get()); }

QString Backend::thermalFrameDataUrl() const { return BackendCoreStateService::thermalFrameDataUrl(d_ptr.get()); }
QString Backend::cctv3dMapFrameDataUrl() const { return BackendCoreStateService::cctv3dMapFrameDataUrl(d_ptr.get()); }
QString Backend::thermalInfoText() const { return BackendCoreStateService::thermalInfoText(d_ptr.get()); }
bool Backend::thermalStreaming() const { return BackendCoreStateService::thermalStreaming(d_ptr.get()); }
QString Backend::thermalPalette() const { return BackendCoreStateService::thermalPalette(d_ptr.get()); }
bool Backend::thermalAutoRange() const { return BackendCoreStateService::thermalAutoRange(d_ptr.get()); }
int Backend::thermalAutoRangeWindowPercent() const { return BackendCoreStateService::thermalAutoRangeWindowPercent(d_ptr.get()); }
int Backend::thermalManualMin() const { return BackendCoreStateService::thermalManualMin(d_ptr.get()); }
int Backend::thermalManualMax() const { return BackendCoreStateService::thermalManualMax(d_ptr.get()); }
int Backend::displayContrast() const { return BackendCoreStateService::displayContrast(d_ptr.get()); }
int Backend::displayBrightness() const { return BackendCoreStateService::displayBrightness(d_ptr.get()); }
int Backend::displaySharpnessLevel() const { return BackendCoreStateService::displaySharpnessLevel(d_ptr.get()); }
bool Backend::displaySharpnessEnabled() const { return BackendCoreStateService::displaySharpnessEnabled(d_ptr.get()); }
int Backend::displayColorLevel() const { return BackendCoreStateService::displayColorLevel(d_ptr.get()); }

void Backend::setActiveCameras(int count)
{
    BackendCoreStateService::setActiveCameras(this, d_ptr.get(), count);
}

void Backend::setCurrentFps(int fps)
{
    BackendCoreStateService::setCurrentFps(this, d_ptr.get(), fps);
}

void Backend::setLatency(int ms)
{
    BackendCoreStateService::setLatency(this, d_ptr.get(), ms);
}

