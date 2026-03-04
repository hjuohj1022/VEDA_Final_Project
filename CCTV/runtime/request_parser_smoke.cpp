#include <iostream>
#include <string>

#include "request.h"
#include "request_validator.h"

namespace {
int failures = 0;

void Expect(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << std::endl;
        ++failures;
    }
}

void TestBasicStartParse() {
    Request req = ParseRequest("channel=2 headless=true");
    Expect(req.channel == 2, "channel should be 2");
    Expect(req.headlessSet && req.headless, "headless=true should be parsed");
}

void TestFloatParseSafety() {
    Request req = ParseRequest("pc_view rx=abc ry=35.5");
    Expect(req.pcView, "pc_view token should be parsed");
    Expect(!req.rxSet, "invalid rx must not be set");
    Expect(req.rySet && req.ry == 35.5f, "valid ry should be set");
}

void TestBoolParseSafety() {
    Request req = ParseRequest("pc_view flipx=maybe wire=ON mesh=0");
    Expect(req.pcView, "pc_view token should be parsed");
    Expect(!req.flipXSet, "invalid bool token must be ignored");
    Expect(req.wireSet && req.wire, "wire=ON should be true");
    Expect(req.meshSet && !req.mesh, "mesh=0 should be false");
}

void TestValidation() {
    {
        Request req = ParseRequest("channel=9");
        RequestValidationResult v = ValidateRequest(req);
        Expect(!v.ok, "channel out of range must fail");
    }
    {
        Request req = ParseRequest("depth_stream rgbd_stream");
        RequestValidationResult v = ValidateRequest(req);
        Expect(!v.ok, "multiple stream requests must fail");
    }
    {
        Request req = ParseRequest("stop");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "stop request should be valid");
    }
}
}  // namespace

int main() {
    TestBasicStartParse();
    TestFloatParseSafety();
    TestBoolParseSafety();
    TestValidation();

    if (failures > 0) {
        std::cerr << "request_parser_smoke failed: " << failures << std::endl;
        return 1;
    }
    std::cout << "request_parser_smoke passed" << std::endl;
    return 0;
}
