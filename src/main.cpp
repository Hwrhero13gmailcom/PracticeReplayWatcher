#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/logging.hpp"
#include "conditional-dependencies/shared/main.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "GlobalNamespace/MenuTransitionsHelper.hpp"
#include "GlobalNamespace/PracticeSettings.hpp"
#include "GlobalNamespace/StandardLevelDetailViewController.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "System/Action_2.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "metacore/shared/events.hpp"
#include "metacore/shared/songs.hpp"
#include "metacore/shared/internals.hpp"

static modloader::ModInfo modInfo{"PracticeReplayWatcher", "0.1.0", 0};
static Paper::ConstLoggerContext<15UL> const logger = Paper::Logger::WithContext<"PracticeReplay">();

static std::string pendingReplayPath = "";
static bool lastMapWasPractice = false;

// ─── helpers ──────────────────────────────────────────────────────────────────

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
            name.ends_with(".bsor"))
            matches.push_back(entry);
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

// ─── hooks ────────────────────────────────────────────────────────────────────

// Track whether the level was started in practice mode
MAKE_AUTO_HOOK_MATCH(
    MenuTransitionsHelper_StartStandardLevel,
    &GlobalNamespace::MenuTransitionsHelper::StartStandardLevel,
    void,
    GlobalNamespace::MenuTransitionsHelper* self,
    StringW f1,
    ByRef<GlobalNamespace::BeatmapKey> f2,
    GlobalNamespace::BeatmapLevel* f3,
    GlobalNamespace::OverrideEnvironmentSettings* f4,
    GlobalNamespace::ColorScheme* f5,
    GlobalNamespace::ColorScheme* f6,
    GlobalNamespace::GameplayModifiers* f7,
    GlobalNamespace::PlayerSpecificSettings* f8,
    GlobalNamespace::PracticeSettings* f9,
    GlobalNamespace::EnvironmentsListModel* f10,
    bool f11,
    bool f12,
    System::Action* f13,
    System::Action_1<::Zenject::DiContainer*>* f14,
    System::Action_2<UnityW<GlobalNamespace::StandardLevelScenesTransitionSetupDataSO>, GlobalNamespace::LevelCompletionResults*>* f15,
    System::Action_2<UnityW<GlobalNamespace::StandardLevelScenesTransitionSetupDataSO>, GlobalNamespace::LevelCompletionResults*>* f16
) {
    lastMapWasPractice = (f9 != nullptr);
    pendingReplayPath = "";
    MenuTransitionsHelper_StartStandardLevel(self, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16);
}

// When returning to menu after a practice session, find the replay file
ON_EVENT(MetaCore::Events::MapEnded) {
    if (!lastMapWasPractice)
        return;
    if (MetaCore::Internals::mapWasQuit)
        return;

    auto mapKey = MetaCore::Songs::GetSelectedKey();
    if (!mapKey.IsValid())
        return;

    std::string mapId = (std::string) mapKey.levelId;

    // Wait for BeatLeader to finish writing
    BSML::MainThreadScheduler::ScheduleAfterTime(1.5f, [mapId]() {
        pendingReplayPath = FindPracticeReplay(mapId);
        if (!pendingReplayPath.empty())
            logger.info("Practice replay ready: {}", pendingReplayPath);
        else
            logger.info("No practice replay found for {}", mapId);
    });
}

// Inject a button on the song detail screen if we have a pending replay
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
    pendingReplayPath = "";

    auto btn = BSML::Lite::CreateUIButton(self->get_transform(), "Watch Practice Replay", [replayPath]() {
        logger.info("Playing practice replay: {}", replayPath);
        PlayReplay(replayPath);
    });

    auto rect = btn->GetComponent<UnityEngine::RectTransform*>();
    rect->set_anchoredPosition({0.0f, -38.0f});
    rect->set_sizeDelta({50.0f, 10.0f});
}

// ─── entry points ─────────────────────────────────────────────────────────────

extern "C" void setup(CModInfo* info) {
    *info = modInfo.to_c();
    logger.info("PracticeReplayWatcher setup");
}

extern "C" void late_load() {
    il2cpp_functions::Init();

    INSTALL_HOOK(logger, MenuTransitionsHelper_StartStandardLevel);
    INSTALL_HOOK(logger, StandardLevelDetailViewController_DidActivate);

    logger.info("PracticeReplayWatcher loaded");
}
