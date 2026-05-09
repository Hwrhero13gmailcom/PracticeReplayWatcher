#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/logging.hpp"
#include "conditional-dependencies/shared/main.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "GlobalNamespace/LevelCompletionResultsViewController.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/PracticeSettings.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "metacore/shared/songs.hpp"
#include "metacore/shared/internals.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/UI/Button.hpp"

static modloader::ModInfo modInfo{"PracticeReplayWatcher", "0.1.0", 0};

static Paper::ConstLoggerContext<15UL> const logger = Paper::Logger::WithContext<"PracticeReplay">();

// Finds the most recent BeatLeader practice bsor for the given map hash + difficulty
static std::string FindPracticeReplay(std::string const& mapId) {
    std::string replayDir = "/sdcard/ModData/com.beatgames.beatsaber/Mods/bl/replays/";

    if (!std::filesystem::exists(replayDir))
        return "";

    // BeatLeader practice files contain "-practice-" in the name
    // and the map hash somewhere in the filename
    // Extract just the hash part (last segment before difficulty)
    // mapId looks like: custom_level_43B2C146C305AE6CEC471B3FB172F420CA8AD47C
    std::string hash = mapId;
    auto prefix = hash.find("custom_level_");
    if (prefix != std::string::npos)
        hash = hash.substr(prefix + 13); // strip "custom_level_"

    std::vector<std::filesystem::directory_entry> matches;

    for (auto const& entry : std::filesystem::directory_iterator(replayDir)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();
        // must contain both "-practice-" and the map hash
        if (name.find("-practice-") != std::string::npos &&
            name.find(hash) != std::string::npos &&
            name.ends_with(".bsor")) {
            matches.push_back(entry);
        }
    }

    if (matches.empty())
        return "";

    // Return the most recently modified file
    std::sort(matches.begin(), matches.end(), [](auto const& a, auto const& b) {
        return a.last_write_time() > b.last_write_time();
    });

    return matches[0].path().string();
}

// Call Replay mod's PlayBSORFromFileForced via conditional-dependencies
static bool PlayReplay(std::string const& path) {
    static auto playFunc = CondDeps::Find<bool, std::string>("replay", "PlayBSORFromFileForced");
    if (!playFunc) {
        logger.error("Could not find PlayBSORFromFileForced — is the Replay mod installed?");
        return false;
    }
    return (*playFunc)(path);
}

MAKE_AUTO_HOOK_MATCH(
    LevelCompletionResultsViewController_DidActivate,
    &GlobalNamespace::LevelCompletionResultsViewController::DidActivate,
    void,
    GlobalNamespace::LevelCompletionResultsViewController* self,
    bool firstActivation,
    bool addedToHierarchy,
    bool screenSystemEnabling
) {
    LevelCompletionResultsViewController_DidActivate(self, firstActivation, addedToHierarchy, screenSystemEnabling);

    if (!firstActivation)
        return;

    // Only add button if this was a practice session
    auto practiceSettings = MetaCore::Internals::practiceSettings;
    if (!practiceSettings)
        return;

    // Get current map id
    auto mapKey = MetaCore::Songs::GetSelectedKey();
    if (!mapKey.IsValid())
        return;

    std::string mapId = (std::string) mapKey.levelId;
    std::string replayPath = FindPracticeReplay(mapId);

    if (replayPath.empty()) {
        logger.info("No practice replay found for {}", mapId);
        return;
    }

    logger.info("Found practice replay: {}", replayPath);

    // Add a "Watch Replay" button to the results screen
    // Find a good parent transform — use the buttons container
    auto parent = self->get_transform();

    auto btn = BSML::Lite::CreateUIButton(parent, "Watch Practice Replay", [replayPath]() {
        logger.info("Playing practice replay: {}", replayPath);
        PlayReplay(replayPath);
    });

    // Position it reasonably — adjust these values if it looks off
    auto rect = btn->GetComponent<UnityEngine::RectTransform*>();
    rect->set_anchoredPosition({0, -40});
}

extern "C" void setup(CModInfo* info) {
    *info = modInfo.to_c();
    logger.info("PracticeReplayWatcher setup complete");
}

extern "C" void late_load() {
    il2cpp_functions::Init();

    Hooks::Install();

    logger.info("PracticeReplayWatcher loaded");
}
