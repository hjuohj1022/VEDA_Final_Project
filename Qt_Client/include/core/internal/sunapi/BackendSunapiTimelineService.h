#ifndef BACKEND_SUNAPI_TIMELINE_SERVICE_H
#define BACKEND_SUNAPI_TIMELINE_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendSunapiTimelineService
{
public:
    static void loadPlaybackTimeline(Backend *backend,
                                     BackendPrivate *state,
                                     int channelIndex,
                                     const QString &dateText);
    static void loadPlaybackMonthRecordedDays(Backend *backend,
                                              BackendPrivate *state,
                                              int channelIndex,
                                              int year,
                                              int month);
};

#endif // BACKEND_SUNAPI_TIMELINE_SERVICE_H
