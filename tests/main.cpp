// Smoke/unit tests for the shared layer. No test framework on purpose: the
// project must stay buildable with nothing but MSVC + CMake + FetchContent.

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "snowydesk/common/Json.h"
#include "snowydesk/common/StateStore.h"
#include "snowydesk/common/Types.h"
#include "snowydesk/ipc/Protocol.h"
#include "snowydesk/snow/SnowWorld.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* expression, const char* file, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL %s:%d  %s\n", file, line, expression);
    }
}

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

void TestSettingsRoundtrip() {
    snowydesk::Settings settings;
    settings.language = snowydesk::Language::English;
    settings.density = 137;
    settings.snowSticks = false;
    settings.garlandEnabled = true;
    settings.autostart = true;
    settings.autostartMode = snowydesk::AutostartMode::Service;
    settings.topmostOverGames = true;

    const auto json = snowydesk::SettingsToJson(settings);
    const auto restored = snowydesk::SettingsFromJson(json);

    CHECK(restored.language == settings.language);
    CHECK(restored.density == settings.density);
    CHECK(restored.snowSticks == settings.snowSticks);
    CHECK(restored.garlandEnabled == settings.garlandEnabled);
    CHECK(restored.autostart == settings.autostart);
    CHECK(restored.autostartMode == settings.autostartMode);
    CHECK(restored.topmostOverGames == settings.topmostOverGames);
}

void TestParticleRoundtrip() {
    snowydesk::Particle particle;
    particle.x = 12.5f;
    particle.y = 40.25f;
    particle.baseX = 10.0f;
    particle.baseY = 39.0f;
    particle.size = 2.5f;
    particle.swayPhase = 1.25f;
    particle.state = snowydesk::ParticleState::OnTaskbar;
    particle.anchor.hwnd = 0x1234;
    particle.anchor.edge = 3;

    const auto json = snowydesk::ParticleToJson(particle);
    const auto restored = snowydesk::ParticleFromJson(json);

    CHECK(restored.x == particle.x);
    CHECK(restored.y == particle.y);
    CHECK(restored.baseX == particle.baseX);
    CHECK(restored.baseY == particle.baseY);
    CHECK(restored.state == particle.state);
    CHECK(restored.anchor.hwnd == particle.anchor.hwnd);
    CHECK(restored.anchor.edge == particle.anchor.edge);
}

void TestStateSchemaValidation() {
    snowydesk::AppState state;
    state.settings.density = 55;
    state.particles.push_back(snowydesk::Particle{});

    const std::string text = snowydesk::SerializeState(state, 2);
    snowydesk::AppState parsed;
    std::string error;
    CHECK(snowydesk::ParseStateText(text, parsed, error));
    CHECK(parsed.schemaVersion == snowydesk::kStateSchemaVersion);
    CHECK(parsed.settings.density == 55);
    CHECK(parsed.particles.size() == 1);

    // Wrong schema version must be rejected.
    CHECK(!snowydesk::ParseStateText(R"({"schemaVersion":99})", parsed, error));
    CHECK(!error.empty());

    // Broken JSON must be rejected.
    CHECK(!snowydesk::ParseStateText("{not json", parsed, error));

    // Density outside of 0..200 is clamped, not rejected.
    auto clamped = snowydesk::SettingsFromJson(nlohmann::json{{"density", 500}});
    CHECK(clamped.density == snowydesk::kDensityMax);
    CHECK(snowydesk::ValidateSettings(clamped).empty());

    snowydesk::Settings invalid;
    invalid.density = -5;
    CHECK(!snowydesk::ValidateSettings(invalid).empty());
}

void TestProtocolRoundtrip() {
    using namespace snowydesk::ipc;

    std::string decodeError;
    const auto ping = DecodeRequest(EncodeRequest(MakePing(7)), decodeError);
    CHECK(ping.has_value());
    if (ping) {
        CHECK(ping->id == 7);
        CHECK(ping->type == MessageType::Ping);
    }

    snowydesk::AppState state;
    state.settings.density = 42;
    state.particles.push_back(snowydesk::Particle{});
    decodeError.clear();
    const auto setState = DecodeRequest(EncodeRequest(MakeSetState(8, state)), decodeError);
    CHECK(setState.has_value());
    if (setState) {
        std::string error;
        const auto payload = ReadStatePayload(setState->payload, error);
        CHECK(payload.has_value());
        if (payload) {
            CHECK(payload->settings.density == 42);
            CHECK(payload->particles.size() == 1);
        }
    }

    snowydesk::Settings settings;
    settings.density = 12;
    decodeError.clear();
    const auto apply = DecodeRequest(EncodeRequest(MakeApplySettings(9, settings)), decodeError);
    CHECK(apply.has_value());
    if (apply) {
        std::string error;
        const auto payload = ReadSettingsPayload(apply->payload, error);
        CHECK(payload.has_value());
        if (payload) CHECK(payload->density == 12);
    }

    decodeError.clear();
    const auto clear = DecodeRequest(EncodeRequest(MakeClearTaskbarSnow(10)), decodeError);
    CHECK(clear.has_value());
    if (clear) CHECK(clear->type == MessageType::ClearTaskbarSnow);

    // Wrong protocol version is rejected with a diagnostic.
    std::string error;
    CHECK(!DecodeRequest(R"({"v":99,"id":1,"type":"ping","payload":{}})", error));
    CHECK(!error.empty());

    // Unknown message type is rejected.
    error.clear();
    CHECK(!DecodeRequest(R"({"v":1,"id":1,"type":"explode","payload":{}})", error));

    // Response roundtrip keeps the error text.
    Response response;
    response.id = 11;
    response.ok = false;
    response.error = "nope";
    const auto decoded = DecodeResponse(EncodeResponse(response), error);
    CHECK(decoded.has_value());
    if (decoded) {
        CHECK(!decoded->ok);
        CHECK(decoded->error == "nope");
    }
}

void TestStateStore() {
    const auto directory =
        std::filesystem::temp_directory_path() / L"snowydesk-tests";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory, ec);

    const auto path = directory / L"state.json";

    snowydesk::AppState state;
    state.settings.density = 99;
    state.particles.push_back(snowydesk::Particle{});

    std::string error;
    CHECK(snowydesk::state_store::Save(path, state, error));
    CHECK(std::filesystem::exists(path));
    CHECK(!std::filesystem::exists(directory / L"state.json.tmp"));

    const auto loaded = snowydesk::state_store::Load(path);
    CHECK(loaded.existed);
    CHECK(!loaded.recoveredFromCorrupt);
    CHECK(loaded.state.settings.density == 99);

    // Corrupt file: must be backed up and defaults returned.
    {
        std::FILE* file = nullptr;
        _wfopen_s(&file, path.c_str(), L"wb");
        CHECK(file != nullptr);
        if (file != nullptr) {
            std::fputs("{ this is not json", file);
            std::fclose(file);
        }
    }

    const auto recovered = snowydesk::state_store::Load(path);
    CHECK(recovered.existed);
    CHECK(recovered.recoveredFromCorrupt);
    CHECK(!recovered.backupPath.empty());
    CHECK(std::filesystem::exists(recovered.backupPath));
    CHECK(!std::filesystem::exists(path));  // moved aside, not silently overwritten

    std::filesystem::remove_all(directory, ec);
}

void TestSnowWorld() {
    snowydesk::snow::SnowWorld world;
    world.SetVirtualRect(RECT{0, 0, 1920, 1080});

    snowydesk::Settings settings;
    settings.density = 40;
    settings.snowSticks = true;
    world.SetSettings(settings);
    world.Reseed(40);
    CHECK(world.particles().size() == 40);

    // Falling flakes must move down over time.
    const float before = world.particles().front().y;
    for (int i = 0; i < 10; ++i) {
        world.Tick(1.0f / 60.0f);
    }
    CHECK(world.particles().front().y >= before);

    // A restored flake whose HWND no longer exists must fall (graceful degradation).
    snowydesk::AppState state;
    snowydesk::Particle anchored;
    anchored.state = snowydesk::ParticleState::OnTopEdge;
    anchored.anchor.hwnd = 0xDEADBEEF;
    anchored.anchor.edge = 1;
    anchored.x = 100.0f;
    anchored.y = 100.0f;
    anchored.baseX = 100.0f;
    anchored.baseY = 100.0f;
    state.particles.push_back(anchored);
    world.AdoptState(state);
    world.PollWindows();

    bool anyFalling = false;
    for (const auto& particle : world.particles()) {
        if (particle.state == snowydesk::ParticleState::Falling) anyFalling = true;
    }
    CHECK(anyFalling);
    CHECK(world.countInState(snowydesk::ParticleState::OnTopEdge) == 0);

    // Taskbar snow can be cleared on demand.
    snowydesk::AppState taskbarState;
    snowydesk::Particle onTaskbar;
    onTaskbar.state = snowydesk::ParticleState::OnTaskbar;
    onTaskbar.anchor.edge = 3;
    onTaskbar.x = 10.0f;
    onTaskbar.y = 10.0f;
    taskbarState.particles.push_back(onTaskbar);
    taskbarState.particles.push_back(onTaskbar);
    world.AdoptState(taskbarState);
    CHECK(world.taskbarPileCount() == 2);
    world.ClearTaskbarSnow();
    CHECK(world.taskbarPileCount() == 0);

    // Density 0 removes everything.
    world.SetSettings([] {
        snowydesk::Settings value;
        value.density = 0;
        return value;
    }());
    world.Reseed(10);
    world.Tick(1.0f / 60.0f);
    CHECK(world.particles().empty());
}

}  // namespace

int main() {
    std::printf("SnowyDesk tests\n");

    TestSettingsRoundtrip();
    TestParticleRoundtrip();
    TestStateSchemaValidation();
    TestProtocolRoundtrip();
    TestStateStore();
    TestSnowWorld();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
