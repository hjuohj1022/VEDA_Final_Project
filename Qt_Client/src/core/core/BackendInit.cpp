#include "Backend.h"
#include "internal/core/BackendInitService.h"
#include "internal/core/Backend_p.h"

#include <memory>

Backend::Backend(QObject *parent)
    // Q Object 초기화 함수
    : QObject(parent), d_ptr(std::make_unique<BackendPrivate>())
{
    BackendInitService::initialize(this, d_ptr.get());
}

Backend::~Backend() {}

bool Backend::isLoggedIn() const { return d_ptr->m_isLoggedIn; }
QString Backend::serverUrl() const { return d_ptr->m_env.value("API_URL", "https://localhost:8080"); }
QString Backend::rtspIp() const { return d_ptr->m_rtspIp; }
QString Backend::rtspPort() const { return d_ptr->m_rtspPort; }

