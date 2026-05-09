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
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "GlobalNamespace/StandardLevelDetailViewController.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "metacore/shared/events.hpp"
#include "metacore/shared/songs.hpp"
#include "metacore/shared/internals.hpp"

static modloader::ModInfo modInfo{"PracticeReplayWatcher", "0.1.0", 0};

static Paper::ConstLoggerContext<15UL> const logger = Paper::Logger::WithContext<"PracticeReplay">();

// Stores the path of the most recently found practice replay
static std::string pendingReplayPath = "";

static std::string FindPracticeReplay(std::string const& mapId) {
    std::string replayDir = "/sdcard/ModData/com.beatgames.beatsaber/Mods/bl/replays/";

    if (!std::filesystem::exists(replayDir))
        return "";

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

// After practice ends, store the replay path so we can show a button when menu loads
ON_EVENT(MetaCore::Events::MapEnded) {
    pendingReplayPath = "";

    if (!MetaCore::Internals::practiceSettings)
        return;
    if (MetaCore::Internals::mapWasQuit)
        return;

    auto mapKey = MetaCore::Songs::GetSelectedKey();
    if (!mapKey.IsValid())
        return;

    std::string mapId = (std::string) mapKey.levelId;

    // Wait 1.5s for BeatLeader to finish writing the file
    BSML::MainThreadScheduler::ScheduleAfterTime(1.5f, [mapId]() {
        pendingReplayPath = FindPracticeReplay(mapId);
        if (!pendingReplayPath.empty())
            logger.info("Practice replay ready: {}", pendingReplayPath);
        else
            logger.info("No practice replay found for {}", mapId);
    });
}

// Hook the level detail view to inject our button when returning from practice
MAKE_AUTO_HOOK_MATCH(
    StandardLevelDetailViewController_DidActivate,
    &GlobalNamespace::StandardLevelDetailViewController::DidActivate,
    void,
    GlobalNamespace::StandardLevelDetailViewController* self,
    bool firstActivation,
    bool addedToHierarchy,
    bool screenSystemEnabling
) {
    StandardLevelDetailViewController_DidActivate(self, firstActivation, addedToHierarchy, screenSystemEnabling);

    if (pendingReplayPath.empty())
        return;

    std::string replayPath = pendingReplayPath;
    pendingReplayPath = ""; // clear so button only shows once

    // Find the action buttons area to attach our button near
    auto parent = self->get_transform();

    auto btn = BSML::Lite::CreateUIButton(parent, "Watch Practice Replay", [replayPath]() {
        logger.info("Playing practice replay: {}", replayPath);
        PlayReplay(replayPath);
    });

    // Position below the normal play/practice buttons
    auto rect = btn->GetComponent<UnityEngine::RectTransform*>();
    rect->set_anchoredPosition({0.0f, -38.0f});
    rect->set_sizeDelta({50.0f, 10.0f});
}

extern "C" void setup(CModInfo* info) {
    *info = modInfo.to_c();
    logger.info("PracticeReplayWatcher setup complete");
}

extern "C" void late_load() {
    il2cpp_functions::Init();
    MetaCore::Events::Setup();
    Hooks::Install();
    logger.info("PracticeReplayWatcher loaded");
}
