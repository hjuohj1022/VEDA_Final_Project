#include "Backend.h"
#include "internal/core/BackendCoreMqttService.h"

void Backend::setupMqtt()
{
    BackendCoreMqttService::setupMqtt(this, d_ptr.get());
}

