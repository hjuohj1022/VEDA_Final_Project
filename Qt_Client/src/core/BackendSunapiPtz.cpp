#include "Backend.h"

#include <QDebug>

// SUNAPI focus 명령을 통한 Zoom/Focus 제어
bool Backend::sunapiZoomIn(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "In"}},
        cameraIndex,
        "Zoom In");
}

bool Backend::sunapiZoomOut(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Out"}},
        cameraIndex,
        "Zoom Out");
}

bool Backend::sunapiZoomStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Stop"}},
        cameraIndex,
        "Zoom Stop");
}

bool Backend::sunapiFocusNear(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Near"}},
        cameraIndex,
        "Focus Near");
}

bool Backend::sunapiFocusFar(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Far"}},
        cameraIndex,
        "Focus Far");
}

bool Backend::sunapiFocusStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Stop"}},
        cameraIndex,
        "Focus Stop");
}

bool Backend::sunapiSimpleAutoFocus(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"Mode", "SimpleFocus"}},
        cameraIndex,
        "Auto Focus");
}


