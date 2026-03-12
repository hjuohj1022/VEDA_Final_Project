#ifndef BACKEND_THERMAL_SERVICE_H
#define BACKEND_THERMAL_SERVICE_H

#include <QByteArray>
#include <QMap>

class Backend;
struct BackendPrivate;
class QString;

class BackendThermalService
{
public:
    static void startThermalStream(Backend *backend, BackendPrivate *state);
    static void stopThermalStream(Backend *backend, BackendPrivate *state);
    static void setThermalPalette(Backend *backend, BackendPrivate *state, const QString &palette);
    static void setThermalAutoRange(Backend *backend, BackendPrivate *state, bool enabled);
    static void setThermalAutoRangeWindowPercent(Backend *backend, BackendPrivate *state, int percent);
    static void setThermalManualRange(Backend *backend, BackendPrivate *state, int minValue, int maxValue);
    static void handleThermalChunkMessage(Backend *backend, BackendPrivate *state, const QByteArray &message);
    static void processThermalFrame(Backend *backend,
                                    BackendPrivate *state,
                                    const QMap<int, QByteArray> &chunks,
                                    int totalChunks,
                                    quint16 minVal,
                                    quint16 maxVal,
                                    int frameId);
};

#endif // BACKEND_THERMAL_SERVICE_H
