#include "Backend.h"
#include "internal/media/BackendMediaRecordingsService.h"

void Backend::refreshRecordings()
{
    BackendMediaRecordingsService::refreshRecordings(this, d_ptr.get());
}

void Backend::deleteRecording(QString name)
{
    BackendMediaRecordingsService::deleteRecording(this, d_ptr.get(), name);
}

void Backend::renameRecording(QString oldName, QString newName)
{
    BackendMediaRecordingsService::renameRecording(this, d_ptr.get(), oldName, newName);
}

QString Backend::getStreamUrl(QString fileName)
{
    return BackendMediaRecordingsService::getStreamUrl(this, fileName);
}

void Backend::downloadAndPlay(QString fileName)
{
    BackendMediaRecordingsService::downloadAndPlay(this, d_ptr.get(), fileName);
}

void Backend::cancelDownload()
{
    BackendMediaRecordingsService::cancelDownload(this, d_ptr.get());
}

void Backend::exportRecording(QString fileName, QString savePath)
{
    BackendMediaRecordingsService::exportRecording(this, d_ptr.get(), fileName, savePath);
}

