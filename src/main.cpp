#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/logging.hpp"
#include "conditional-dependencies/shared/main.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/PracticeSettings.hpp"
#include "metacore/shared/events.hpp"
#include "metacore/shared/songs.hpp"
#include "metacore/shared/internals.hpp"

static modloader::ModInfo modInfo{"PracticeReplayWatcher", "0.1.0", 0};

static Paper::ConstLoggerContext<15UL> const logger = Paper::Logger::WithContext<"PracticeReplay">();

// Finds the most recent BeatLeader practice bsor for the given map hash
static std::string FindPracticeReplay(std::string const& mapId) {
    std::string replayDir = "/sdcard/ModData/com.beatgames.beatsaber/Mods/bl/replays/";

    if (!std::filesystem::exists(replayDir))
        return "";

    // Strip "custom_level_" prefix to get the raw hash
    std::string hash = mapId;
    auto prefix = hash.find("custom_level_");
    if (prefix != std::string::npos)
        hash = hash.substr(prefix + 13);

    std::vector<std::filesystem::directory_entry> matches;

    for (auto const& entry : std::filesystem::directory_iterator(replayDir)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();
        if (name.find("-practice-") != std::string::npos &&
            name.find(hash) != std::string::npos &&
            name.ends_with(".bsor")) {
            matches.push_back(entry);
        }
    }

    if (matches.empty())
        return "";

    std::sort(matches.begin(), matches.end(), [](auto const& a, auto const& b) {
        return a.last_write_time() > b.last_write_time();
    });

    return matches[0].path().string();
}

static bool PlayReplay(std::string const& path) {
    static auto playFunc = CondDeps::Find<bool, std::string>("replay", "PlayBSORFromFileForced");
    if (!playFunc) {
        logger.error("Could not find PlayBSORFromFileForced — is the Replay mod installed?");
        return false;
    }
    return (*playFunc)(path);
}

ON_EVENT(MetaCore::Events::MapEnded) {
    // Only care about practice sessions
    if (!MetaCore::Internals::practiceSettings)
        return;

    // Don't show button if they quit
    if (MetaCore::Internals::mapWasQuit)
        return;

    auto mapKey = MetaCore::Songs::GetSelectedKey();
    if (!mapKey.IsValid())
        return;

    std::string mapId = (std::string) mapKey.levelId;

    // Give BeatLeader a moment to finish writing the file, then check
    BSML::MainThreadScheduler::ScheduleAfterTime(1.5f, [mapId]() {
        std::string replayPath = FindPracticeReplay(mapId);
        if (replayPath.empty()) {
            logger.info("No practice replay found for {}", mapId);
            return;
        }

        logger.info("Found practice replay: {}", replayPath);
        PlayReplay(replayPath);
    });
}

extern "C" void setup(CModInfo* info) {
    *info = modInfo.to_c();
    logger.info("PracticeReplayWatcher setup complete");
}

extern "C" void late_load() {
    il2cpp_functions::Init();
    MetaCore::Events::Setup();
    logger.info("PracticeReplayWatcher loaded");
}
