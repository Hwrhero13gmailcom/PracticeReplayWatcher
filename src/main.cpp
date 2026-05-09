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
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/ColorScheme.hpp"
#include "GlobalNamespace/GameplayModifiers.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/RecordingToolManager.hpp"
#include "System/Action.hpp"
#include "System/Action_1.hpp"
#include "System/Action_2.hpp"
#include "System/Nullable_1.hpp"
#include "Zenject/DiContainer.hpp"
#include "metacore/shared/events.hpp"
#include "metacore/shared/songs.hpp"
#include "metacore/shared/internals.hpp"

using namespace GlobalNamespace;

static modloader::ModInfo modInfo{"PracticeReplayWatcher", "0.1.0", 0};
static Paper::ConstLoggerContext<15UL> const logger = Paper::Logger::WithContext<"PracticeReplay">();

static bool lastMapWasPractice = false;
static std::string pendingReplayPath = "";

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

static void PlayReplay(std::string const& path) {
    static auto playFunc = CondDeps::Find<bool, std::string>("replay", "PlayBSORFromFileForced");
    if (!playFunc) {
        logger.error("Could not find PlayBSORFromFileForced — is the Replay mod installed?");
        return;
    }
    (*playFunc)(path);
}

// ─── hooks ────────────────────────────────────────────────────────────────────

MAKE_AUTO_HOOK_MATCH(
    MenuTransitionsHelper_StartStandardLevel,
    &MenuTransitionsHelper::StartStandardLevel,
    void,
    MenuTransitionsHelper* self,
    StringW f1,
    ByRef<BeatmapKey> f2,
    BeatmapLevel* f3,
    OverrideEnvironmentSettings* f4,
    ColorScheme* f5,
    bool f6,
    ColorScheme* f7,
    GameplayModifiers* f8,
    PlayerSpecificSettings* f9,
    PracticeSettings* f10,
    EnvironmentsListModel* f11,
    StringW f12,
    bool f13,
    bool f14,
    System::Action* f15,
    System::Action_1<Zenject::DiContainer*>* f16,
    System::Action_2<UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults*>* f17,
    System::Action_2<UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults*>* f18,
    System::Nullable_1<RecordingToolManager::SetupData> f19
) {
    lastMapWasPractice = (f10 != nullptr);
    pendingReplayPath = "";
    MenuTransitionsHelper_StartStandardLevel(self, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19);
}

// After practice ends, find the replay and show a popup button
ON_EVENT(MetaCore::Events::MapEnded) {
    if (!lastMapWasPractice)
        return;
    if (MetaCore::Internals::mapWasQuit)
        return;

    auto mapKey = MetaCore::Songs::GetSelectedKey();
    if (!mapKey.IsValid())
        return;

    std::string mapId = (std::string) mapKey.levelId;

    // Wait 1.5s for BeatLeader to finish writing the file
    BSML::MainThreadScheduler::ScheduleAfterTime(1.5f, [mapId]() {
        std::string path = FindPracticeReplay(mapId);
        if (path.empty()) {
            logger.info("No practice replay found for {}", mapId);
            return;
        }
        logger.info("Practice replay ready: {}", path);

        // Show a floating button using BSML modal
        BSML::Lite::CreateCanvas();  // ensure UI canvas exists
        auto screen = BSML::Lite::CreateFloatingScreen({40, 15}, {0, 3, 4}, {0, 0, 0}, 0, false, true);
        auto btn = BSML::Lite::CreateUIButton(screen->get_transform(), "Watch Practice Replay", [path, screen]() {
            UnityEngine::Object::Destroy(screen);
            PlayReplay(path);
        });
        auto closeBtn = BSML::Lite::CreateUIButton(screen->get_transform(), "X", [screen]() {
            UnityEngine::Object::Destroy(screen);
        });

        auto btnRect = btn->GetComponent<UnityEngine::RectTransform*>();
        btnRect->set_anchoredPosition({-5.0f, 0.0f});

        auto closeRect = closeBtn->GetComponent<UnityEngine::RectTransform*>();
        closeRect->set_anchoredPosition({15.0f, 0.0f});
        closeRect->set_sizeDelta({8.0f, 8.0f});
    });
}

// ─── entry points ─────────────────────────────────────────────────────────────

extern "C" void setup(CModInfo* info) {
    *info = modInfo.to_c();
    logger.info("PracticeReplayWatcher setup");
}

extern "C" void late_load() {
    il2cpp_functions::Init();
    INSTALL_HOOK(logger, MenuTransitionsHelper_StartStandardLevel);
    logger.info("PracticeReplayWatcher loaded");
}
