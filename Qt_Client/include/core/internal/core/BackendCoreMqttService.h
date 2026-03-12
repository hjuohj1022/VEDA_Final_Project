#ifndef BACKEND_CORE_MQTT_SERVICE_H
#define BACKEND_CORE_MQTT_SERVICE_H

class Backend;
struct BackendPrivate;

class BackendCoreMqttService
{
public:
    static void setupMqtt(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_CORE_MQTT_SERVICE_H
