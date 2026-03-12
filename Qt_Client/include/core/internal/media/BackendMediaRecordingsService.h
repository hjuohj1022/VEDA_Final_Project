#ifndef BACKEND_MEDIA_RECORDINGS_SERVICE_H
#define BACKEND_MEDIA_RECORDINGS_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendMediaRecordingsService
{
public:
    static void refreshRecordings(Backend *backend, BackendPrivate *state);
    static void deleteRecording(Backend *backend, BackendPrivate *state, QString name);
    static void renameRecording(Backend *backend, BackendPrivate *state, QString oldName, QString newName);
    static QString getStreamUrl(Backend *backend, QString fileName);
    static void downloadAndPlay(Backend *backend, BackendPrivate *state, QString fileName);
    static void cancelDownload(Backend *backend, BackendPrivate *state);
    static void exportRecording(Backend *backend, BackendPrivate *state, QString fileName, QString savePath);
};

#endif // BACKEND_MEDIA_RECORDINGS_SERVICE_H
