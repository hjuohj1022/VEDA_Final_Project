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

void TestValidationChannelBoundaries() {
    {
        Request req = ParseRequest("channel=-1");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "channel=-1(default) should be valid");
    }
    {
        Request req = ParseRequest("channel=0");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "channel=0 should be valid");
    }
    {
        Request req = ParseRequest("channel=3");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "channel=3 should be valid");
    }
    {
        Request req = ParseRequest("channel=-2");
        RequestValidationResult v = ValidateRequest(req);
        Expect(!v.ok, "channel=-2 should be invalid");
    }
    {
        Request req = ParseRequest("channel=4");
        RequestValidationResult v = ValidateRequest(req);
        Expect(!v.ok, "channel=4 should be invalid");
    }
}

void TestValidationStreamCombinations() {
    {
        Request req = ParseRequest("depth_stream");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "single depth_stream should be valid");
    }
    {
        Request req = ParseRequest("rgbd_stream");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "single rgbd_stream should be valid");
    }
    {
        Request req = ParseRequest("pc_stream");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "single pc_stream should be valid");
    }
    {
        Request req = ParseRequest("depth_stream pc_stream");
        RequestValidationResult v = ValidateRequest(req);
        Expect(!v.ok, "depth_stream + pc_stream should be invalid");
    }
    {
        Request req = ParseRequest("rgbd_stream pc_stream");
        RequestValidationResult v = ValidateRequest(req);
        Expect(!v.ok, "rgbd_stream + pc_stream should be invalid");
    }
    {
        Request req = ParseRequest("depth_stream rgbd_stream pc_stream");
        RequestValidationResult v = ValidateRequest(req);
        Expect(!v.ok, "three stream requests should be invalid");
    }
}

void TestValidationMixedControlCommands() {
    {
        Request req = ParseRequest("channel=2 pause");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "channel + pause should be valid");
    }
    {
        Request req = ParseRequest("channel=1 resume");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "channel + resume should be valid");
    }
    {
        Request req = ParseRequest("channel=1 headless gui");
        RequestValidationResult v = ValidateRequest(req);
        Expect(v.ok, "headless/gui mixed token should remain valid");
        Expect(req.gui, "gui token should be set");
        Expect(req.headlessSet && !req.headless, "gui should force non-headless");
    }
}

void TestStatusParse() {
    {
        Request req = ParseRequest("status");
        Expect(req.statusQuery, "status token should be parsed");
    }
    {
        Request req = ParseRequest("worker_status");
        Expect(req.statusQuery, "worker_status alias should be parsed");
    }
}
}  // namespace

int main() {
    TestBasicStartParse();
    TestFloatParseSafety();
    TestBoolParseSafety();
    TestValidation();
    TestValidationChannelBoundaries();
    TestValidationStreamCombinations();
    TestValidationMixedControlCommands();
    TestStatusParse();

    if (failures > 0) {
        std::cerr << "request_parser_smoke failed: " << failures << std::endl;
        return 1;
    }
    std::cout << "request_parser_smoke passed" << std::endl;
    return 0;
}
