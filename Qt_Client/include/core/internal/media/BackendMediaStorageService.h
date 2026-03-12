#ifndef BACKEND_MEDIA_STORAGE_SERVICE_H
#define BACKEND_MEDIA_STORAGE_SERVICE_H

class Backend;
struct BackendPrivate;

class BackendMediaStorageService
{
public:
    static void checkStorage(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_MEDIA_STORAGE_SERVICE_H
