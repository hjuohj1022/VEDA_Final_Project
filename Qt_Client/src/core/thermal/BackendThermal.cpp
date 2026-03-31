#include "Backend.h"
#include "internal/thermal/BackendThermalService.h"

// 열화상 스트림 시작 함수
void Backend::startThermalStream()
{
    BackendThermalService::startThermalStream(this, d_ptr.get());
}

// 열화상 스트림 중지 함수
void Backend::stopThermalStream()
{
    BackendThermalService::stopThermalStream(this, d_ptr.get());
}

// 열화상 팔레트 설정 함수
void Backend::setThermalPalette(const QString &palette)
{
    BackendThermalService::setThermalPalette(this, d_ptr.get(), palette);
}

// 열화상 자동 범위 설정 함수
void Backend::setThermalAutoRange(bool enabled)
{
    BackendThermalService::setThermalAutoRange(this, d_ptr.get(), enabled);
}

// 열화상 자동 범위 창 Percent 설정 함수
void Backend::setThermalAutoRangeWindowPercent(int percent)
{
    BackendThermalService::setThermalAutoRangeWindowPercent(this, d_ptr.get(), percent);
}

// 열화상 수동 범위 설정 함수
void Backend::setThermalManualRange(int minValue, int maxValue)
{
    BackendThermalService::setThermalManualRange(this, d_ptr.get(), minValue, maxValue);
}

// 열화상 청크 메시지 처리 함수
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

