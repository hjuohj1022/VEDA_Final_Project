#include "Backend.h"
#include "internal/cctv/BackendCctv3dMapService.h"

bool Backend::startCctv3dMapPrepareSequence(int cameraIndex)
{
    return BackendCctv3dMapService::startCctv3dMapPrepareSequence(this, d_ptr.get(), cameraIndex);
}
bool Backend::startCctv3dMapSequence(int cameraIndex)
{
    return BackendCctv3dMapService::startCctv3dMapSequence(this, d_ptr.get(), cameraIndex);
}

bool Backend::pauseCctv3dMapSequence()
{
    return BackendCctv3dMapService::pauseCctv3dMapSequence(this, d_ptr.get());
}

bool Backend::resumeCctv3dMapSequence()
{
    return BackendCctv3dMapService::resumeCctv3dMapSequence(this, d_ptr.get());
}

void Backend::stopCctv3dMapSequence()
{
    BackendCctv3dMapService::stopCctv3dMapSequence(this, d_ptr.get());
}

bool Backend::updateCctv3dMapView(double rx, double ry)
{
    return BackendCctv3dMapService::postCctvControlView(this, d_ptr.get(), rx, ry);
}

void Backend::runCctv3dMapSequenceStep(int sequenceToken, int step)
{
    BackendCctv3dMapService::runCctv3dMapSequenceStep(this, d_ptr.get(), sequenceToken, step);
}

void Backend::pollCctv3dMapMoveStatus(int sequenceToken)
{
    BackendCctv3dMapService::pollCctv3dMapMoveStatus(this, d_ptr.get(), sequenceToken);
}

bool Backend::postCctvControlStart(int sequenceToken)
{
    return BackendCctv3dMapService::postCctvControlStart(this, d_ptr.get(), sequenceToken);
}

bool Backend::postCctvControlStream(int sequenceToken)
{
    return BackendCctv3dMapService::postCctvControlStream(this, d_ptr.get(), sequenceToken);
}

void Backend::connectCctvStreamWs(int sequenceToken)
{
    BackendCctv3dMapService::connectCctvStreamWs(this, d_ptr.get(), sequenceToken);
}

void Backend::disconnectCctvStreamWs(bool expectedStop)
{
    BackendCctv3dMapService::disconnectCctvStreamWs(this, d_ptr.get(), expectedStop);
}

