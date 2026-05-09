#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <functional>

#include "beatsaber-hook/shared/utils/logging.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "conditional-dependencies/shared/main.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Object.hpp"
#include "metacore/shared/events.hpp"
#include "metacore/shared/songs.hpp"
#include "metacore/shared/internals.hpp"

static modloader::ModInfo modInfo{"PracticeReplayWatcher", "0.1.0", 0};
static Paper::ConstLoggerContext<15UL> const logger = Paper::Logger::WithContext<"PracticeReplay">();

// Queue of callbacks to run on the main thread, checked via Update event
static std::mutex pendingMutex;
static std::vector<std::function<void()>> pendingCallbacks;

static void RunOnMainThread(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(pendingMutex);
    pendingCallbacks.push_back(std::move(fn));
}

// ─── helpers ──────────────────────────────────────────────────────────────────

static std::string FindRecentPracticeReplay(std::string const& mapId) {
    std::string replayDir = "/sdcard/ModData/com.beatgames.beatsaber/Mods/bl/replays/";
    if (!std::filesystem::exists(replayDir))
        return "";

    std::string hash = mapId;
    auto prefix = hash.find("custom_level_");
    if (prefix != std::string::npos)
        hash = hash.substr(prefix + 13);

    auto now = std::filesystem::file_time_type::clock::now();
    std::vector<std::filesystem::directory_entry> matches;

    for (auto const& entry : std::filesystem::directory_iterator(replayDir)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();
        if (name.find("-practice-") == std::string::npos) continue;
        if (name.find(hash) == std::string::npos) continue;
        if (!name.ends_with(".bsor")) continue;
        auto age = now - entry.last_write_time();
        if (age < std::chrono::seconds(30))
            matches.push_back(entry);
    }

    if (matches.empty())
        return "";

    std::sort(matches.begin(), matches.end(), [](auto const& a, auto const& b) {
        return a.last_write_time() > b.last_write_time();
    });
    return matches[0].path().string();
}

static void PlayReplay(std::string const& path) {
    static auto playFunc = CondDeps::Find<bool, std::string>("replay", "PlayBSORFromFileForced");
    if (!playFunc) {
        logger.error("Could not find PlayBSORFromFileForced — is the Replay mod installed?");
        return;
    }
    (*playFunc)(path);
}

static void ShowReplayButton(std::string const& path) {
    auto screen = BSML::Lite::CreateFloatingScreen(
        {50, 15}, {0, 3, 4}, {0, 0, 0}, 0, false, true
    );

    BSML::Lite::CreateUIButton(
        screen->get_transform(), "Watch Practice Replay",
        [path, screen]() {
            UnityEngine::Object::Destroy(screen);
            PlayReplay(path);
        }
    )->GetComponent<UnityEngine::RectTransform*>()->set_anchoredPosition({-8.0f, 0.0f});

    BSML::Lite::CreateUIButton(
        screen->get_transform(), "X",
        [screen]() { UnityEngine::Object::Destroy(screen); }
    )->GetComponent<UnityEngine::RectTransform*>()->set_anchoredPosition({18.0f, 0.0f});
}

// ─── events ───────────────────────────────────────────────────────────────────

// Drain pending main-thread callbacks every frame
ON_EVENT(MetaCore::Events::Update) {
    std::vector<std::function<void()>> toRun;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        toRun.swap(pendingCallbacks);
    }
    for (auto& fn : toRun)
        fn();
}

ON_EVENT(MetaCore::Events::MapEnded) {
    if (MetaCore::Internals::mapWasQuit)
        return;

    auto mapKey = MetaCore::Songs::GetSelectedKey();
    if (!mapKey.IsValid())
        return;

    std::string mapId = (std::string) mapKey.levelId;

    std::thread([mapId]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        std::string path = FindRecentPracticeReplay(mapId);
        if (path.empty()) {
            logger.info("No recent practice replay found for {}", mapId);
            return;
        }

        logger.info("Practice replay found: {}", path);
        RunOnMainThread([path]() { ShowReplayButton(path); });
    }).detach();
}

// ─── entry points ─────────────────────────────────────────────────────────────

extern "C" void setup(CModInfo* info) {
    *info = modInfo.to_c();
    logger.info("PracticeReplayWatcher setup");
}

extern "C" void late_load() {
    il2cpp_functions::Init();
    logger.info("PracticeReplayWatcher loaded");
}
