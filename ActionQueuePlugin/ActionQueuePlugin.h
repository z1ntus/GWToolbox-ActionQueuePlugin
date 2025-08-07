#pragma once

#include <ToolboxUIPlugin.h>

#include <GWCA/Managers/AgentMgr.h>

#include <GWCA/Packets/StoC.h>

#include <GWCA/Utilities/Hook.h>

#include <GWCA/GameContainers/Array.h>
//20250624

#include <queue>
#include <functional>
#include <filesystem>

enum class ActionType {
    Move,
    EnterChallenge,
    EnterPortal,
    InteractAgent,
    DropItem,
    Wait,
    UseSkill,
    WaitForIdle,
    InteractNearestItemWithinDistance,
    TargetNearestEnemyAndAttack,
    ReturnToOutpost,
    ApproachNearestEnemy,
    TargetNextEnemy,
    TargetPreviousEnemy,
    TargetStrongestEnemyWithinDistance,
    TargetNearestEnemyWithinDistance
    // You can add more actions like CastSkill, DropItem, etc.
};

struct QueuedAction {
    ActionType type;
    float x = 0, y = 0;            // For Move
    float max_item_distance = 0.f; // For TargetNearestItemWithinDistance
    float approach_distance = 1000;

    uint32_t agent_id = 0;         // For InteractAgent
    uint32_t skill_id = 0;         // For UseSkill
    uint32_t skill_target_id = 0;  // New: Target for UseSkill
    uint64_t wait_until = 0;       // For Wait
    //bool wait_initialized = false; // Marks if wait_until is already set
    //bool skill_triggered = false;  // default = false
    //bool was_not_idle = false;     // Used by WaitForIdle
    uint64_t started_timestamp = 0; // Unix time in milliseconds


    // per‑action state:
    bool is_moving = false;
    bool entered = false;
    bool is_interacting = false;
    bool wait_initialized = false;
    bool skill_triggered = false;
    bool was_not_idle = false;
    bool was_attacking = false;
    bool became_not_attacking = false;
    bool item_targeted = false;
    bool item_interacted = false;

    bool cast_failed = false;
    bool cast_in_progress = false;
    //bool was_casting = false;

    // For actions like EnterChallenge, DropItem
    explicit QueuedAction(ActionType type) : type(type) {}

    // For Move
    QueuedAction(ActionType type, float x, float y) : type(type), x(x), y(y) {}

    // For InteractAgent
    QueuedAction(ActionType type, uint32_t agent_id) : type(type), agent_id(agent_id) {}

    // Static factory method for Wait
    static QueuedAction MakeWait(uint64_t duration_ms)
    {
        QueuedAction a(ActionType::Wait);
        a.wait_until = duration_ms; // TEMPORARILY store duration here
        return a;
    }

    static QueuedAction MakeUseSkill(uint32_t skill_id, uint32_t target_id)
    {
        QueuedAction a(ActionType::UseSkill);
        a.skill_id = skill_id;
        a.skill_target_id = target_id;
        return a;
    }

    static QueuedAction MakeInteractNearestItemWithinDistance(float max_distance)
    {
        QueuedAction a(ActionType::InteractNearestItemWithinDistance);
        a.max_item_distance = max_distance;
        return a;
    }

    static QueuedAction MakeApproachNearestEnemy(float within_distance)
    {
        QueuedAction a(ActionType::ApproachNearestEnemy);
        a.approach_distance = within_distance;
        return a;
    }

    static QueuedAction MakeTargetNextEnemy() 
    { 
        return QueuedAction(ActionType::TargetNextEnemy); 
    }

    static QueuedAction MakeTargetPreviousEnemy() 
    { 
        return QueuedAction(ActionType::TargetPreviousEnemy); 
    }

    static QueuedAction MakeTargetStrongestEnemy(float within_distance)
    {
        QueuedAction a(ActionType::TargetStrongestEnemyWithinDistance);
        a.approach_distance = within_distance;
        return a;
    }

    static QueuedAction MakeTargetNearestEnemyWithinDistance(float within_distance)
    {
        QueuedAction a(ActionType::TargetNearestEnemyWithinDistance);
        a.approach_distance = within_distance;
        return a;
    }
};

class ActionQueuePlugin : public ToolboxPlugin {
public:
    ActionQueuePlugin() = default;
    ~ActionQueuePlugin() override = default;

    const char* Name() const override { return "Example Plugin"; }

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;
    [[nodiscard]] bool HasSettings() const override { return true; }
    void DrawSettings() override;
    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
    bool CanTerminate() override;
    // Draw user interface. Will be called every frame if the element is visible
    void Draw(IDirect3DDevice9* pDevice) override;
    //20250624
    void Update(float /*delta*/) override;
    //20250625
    void QueueMove(float x, float y);
    void QueueEnterChallenge();
    void QueueReturnToOutpost();
    void QueueInteract(uint32_t agent_id = 0);
    void QueueDropItem();
    void QueueWait(uint64_t duration_ms);
    void QueueUseSkill(uint32_t skill_id, uint32_t target_id = 0);
    void QueueWaitForIdle();
    void QueueInteractNearestItemWithinDistance(float max_distance);
    void QueueTargetNearestEnemyAndAttack();
    void QueueApproachNearestEnemy(float within_distance);
    void QueueEnterPortal(float x, float y);
    void QueueTargetNextEnemy();
    void QueueTargetPreviousEnemy();
    void QueueTargetStrongestEnemyWithinDistance(float within_distance);
    void QueueTargetNearestEnemyWithinDistance(float within_distance);
    // Hook callbacks
    void OnGenericValuePacket(GW::HookStatus* status, GW::Packet::StoC::GenericValue* packet);
    void HandleCastFailMessage(GW::HookStatus*, GW::Packet::StoC::MessageCore* packet);
    void LoadScriptFromFile(const std::string& filename);

    bool repeat_script = false;
    std::string current_script;
    std::chrono::steady_clock::time_point last_finished;
    bool was_empty = false;

private:
    std::queue<QueuedAction> action_queue;
    bool is_moving = false;
    bool is_interacting = false;
    bool maintain_active = true;
    const float reach_threshold = 50.0f; // Adjust as needed

    std::chrono::steady_clock::time_point travel_time{};
    bool waiting_for_confirm = false;

    GW::HookEntry hook_generic_value;
    GW::HookEntry chat_error_hook_entry;

    std::vector<std::string> bot_files;
    int selected_bot_index = -1;
};

namespace ActionQueueUtils {
    bool PathGetDocumentsPath(std::filesystem::path& out, const wchar_t* suffix = nullptr);
}
