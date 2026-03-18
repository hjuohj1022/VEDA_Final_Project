#include "Backend.h"
#include "internal/thermal/BackendThermalService.h"

void Backend::startThermalStream()
{
    BackendThermalService::startThermalStream(this, d_ptr.get());
}

void Backend::stopThermalStream()
{
    BackendThermalService::stopThermalStream(this, d_ptr.get());
}

void Backend::setThermalPalette(const QString &palette)
{
    BackendThermalService::setThermalPalette(this, d_ptr.get(), palette);
}

void Backend::setThermalAutoRange(bool enabled)
{
    BackendThermalService::setThermalAutoRange(this, d_ptr.get(), enabled);
}

void Backend::setThermalAutoRangeWindowPercent(int percent)
{
    BackendThermalService::setThermalAutoRangeWindowPercent(this, d_ptr.get(), percent);
}

void Backend::setThermalManualRange(int minValue, int maxValue)
{
    BackendThermalService::setThermalManualRange(this, d_ptr.get(), minValue, maxValue);
}

void Backend::handleThermalChunkMessage(const QByteArray &message)
{
    BackendThermalService::handleThermalChunkMessage(this, d_ptr.get(), message);
}

void Backend::processThermalFrame(const QMap<int, QByteArray> &chunks,
                                  int totalChunks,
                                  quint16 minVal,
                                  quint16 maxVal,
                                  int frameId)
{
    BackendThermalService::processThermalFrame(this, d_ptr.get(), chunks, totalChunks, minVal, maxVal, frameId);
}

