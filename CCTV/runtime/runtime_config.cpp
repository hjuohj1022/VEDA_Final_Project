#include "runtime_config.h"

const RuntimeConfig& GetRuntimeConfig() {
    static const RuntimeConfig cfg{};
    return cfg;
}
