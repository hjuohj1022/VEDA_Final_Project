#include "Backend.h"
#include "internal/sunapi/BackendSunapiDisplayService.h"

// Sunapi 화면 설정 로드 함수
void Backend::sunapiLoadDisplaySettings(int cameraIndex)
{
    BackendSunapiDisplayService::sunapiLoadDisplaySettings(this, d_ptr.get(), cameraIndex);
}

// Sunapi 화면 설정 적용 함수
bool Backend::sunapiSetDisplaySettings(int cameraIndex,
                                       int contrast,
                                       int brightness,
                                       int sharpnessLevel,
                                       int colorLevel,
                                       bool sharpnessEnabled)
{
    return BackendSunapiDisplayService::sunapiSetDisplaySettings(this,
                                                                 d_ptr.get(),
                                                                 cameraIndex,
                                                                 contrast,
                                                                 brightness,
                                                                 sharpnessLevel,
                                                                 colorLevel,
                                                                 sharpnessEnabled);
}

// Sunapi 화면 설정 초기화 함수
bool Backend::sunapiResetDisplaySettings(int cameraIndex)
{
    return BackendSunapiDisplayService::sunapiResetDisplaySettings(this, d_ptr.get(), cameraIndex);
}

