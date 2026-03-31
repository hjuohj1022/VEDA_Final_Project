#include "Backend.h"
#include "internal/core/BackendCoreMqttService.h"

// 설정 MQTT 처리 함수
void Backend::setupMqtt()
{
    BackendCoreMqttService::setupMqtt(this, d_ptr.get());
}

