#include "Backend.h"
#include "internal/cctv/BackendCctv3dMapService.h"

// Cctv3d 맵 Prepare Sequence 시작 함수
bool Backend::startCctv3dMapPrepareSequence(int cameraIndex)
{
    return BackendCctv3dMapService::startCctv3dMapPrepareSequence(this, d_ptr.get(), cameraIndex);
}
// Cctv3d 맵 Sequence 시작 함수
bool Backend::startCctv3dMapSequence(int cameraIndex)
{
    return BackendCctv3dMapService::startCctv3dMapSequence(this, d_ptr.get(), cameraIndex);
}

// Cctv3d 맵 Sequence 일시정지 함수
bool Backend::pauseCctv3dMapSequence()
{
    return BackendCctv3dMapService::pauseCctv3dMapSequence(this, d_ptr.get());
}

// Cctv3d 맵 Sequence 재개 함수
bool Backend::resumeCctv3dMapSequence()
{
    return BackendCctv3dMapService::resumeCctv3dMapSequence(this, d_ptr.get());
}

// Cctv3d 맵 Sequence 중지 함수
void Backend::stopCctv3dMapSequence()
{
    BackendCctv3dMapService::stopCctv3dMapSequence(this, d_ptr.get());
}

// Cctv3d 맵 뷰 갱신 함수
bool Backend::updateCctv3dMapView(double rx, double ry)
{
    return BackendCctv3dMapService::postCctvControlView(this, d_ptr.get(), rx, ry);
}

// Cctv3d 맵 Sequence Step 실행 함수
void Backend::runCctv3dMapSequenceStep(int sequenceToken, int step)
{
    BackendCctv3dMapService::runCctv3dMapSequenceStep(this, d_ptr.get(), sequenceToken, step);
}

// Cctv3d 맵 이동 상태 폴링 함수
void Backend::pollCctv3dMapMoveStatus(int sequenceToken)
{
    BackendCctv3dMapService::pollCctv3dMapMoveStatus(this, d_ptr.get(), sequenceToken);
}

// CCTV 제어 시작 전송 함수
bool Backend::postCctvControlStart(int sequenceToken)
{
    return BackendCctv3dMapService::postCctvControlStart(this, d_ptr.get(), sequenceToken);
}

// CCTV 제어 스트림 전송 함수
bool Backend::postCctvControlStream(int sequenceToken)
{
    return BackendCctv3dMapService::postCctvControlStream(this, d_ptr.get(), sequenceToken);
}

// CCTV 스트림 WebSocket 연결 함수
void Backend::connectCctvStreamWs(int sequenceToken)
{
    BackendCctv3dMapService::connectCctvStreamWs(this, d_ptr.get(), sequenceToken);
}

// CCTV 스트림 WebSocket 연결 해제 함수
void Backend::disconnectCctvStreamWs(bool expectedStop)
{
    BackendCctv3dMapService::disconnectCctvStreamWs(this, d_ptr.get(), expectedStop);
}

