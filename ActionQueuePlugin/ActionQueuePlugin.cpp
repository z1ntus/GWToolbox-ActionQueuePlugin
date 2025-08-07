#include "ActionQueuePlugin.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Utilities/Hook.h>

#include <Utils/GuiUtils.h>
#include "GWCA/Managers/ChatMgr.h"

#include <imgui.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Skill.h>

#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/EffectMgr.h>
#include <GWCA/Managers/StoCMgr.h>

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>

struct TooltipInfo {
    uint32_t bit_field;              // unknown
    void* payload;                   // wchar_t* string pointer
    uint32_t payload_len;            // length in bytes or characters
    uint32_t render;                 // unknown
    uint32_t unk1, unk2, unk3, unk4; // unknown
};

struct FrameContext {
    uint32_t unk1[3];          // padding or unknown fields
    TooltipInfo* tooltip_info; // guessed position for tooltip
    // Add more fields if needed
};

namespace {
    bool redirect_slash_ee_to_eee = false;
    GW::HookEntry ChatCmd_HookEntry;
}

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static ActionQueuePlugin instance;
    return &instance;
}

void ActionQueuePlugin::LoadSettings(const wchar_t* folder)
{
    ToolboxPlugin::LoadSettings(folder);
    PLUGIN_LOAD_BOOL(redirect_slash_ee_to_eee);
}

void ActionQueuePlugin::SaveSettings(const wchar_t* folder)
{
    PLUGIN_SAVE_BOOL(redirect_slash_ee_to_eee);
    ToolboxPlugin::SaveSettings(folder);
}

namespace {

    // helper to turn ActionType into text
    static const char* ActionTypeToString(ActionType t)
    {
        switch (t) {
            case ActionType::Move:
                return "Move";
            case ActionType::EnterChallenge:
                return "EnterChallenge";
            case ActionType::InteractAgent:
                return "InteractAgent";
            case ActionType::DropItem:
                return "DropItem";
            case ActionType::Wait:
                return "Wait";
            case ActionType::UseSkill:
                return "UseSkill";
            case ActionType::WaitForIdle:
                return "WaitForIdle";
            case ActionType::InteractNearestItemWithinDistance:
                return "InteractNearestItemWithinDistance";
            case ActionType::TargetNearestEnemyAndAttack:
                return "TargetNearestEnemyAndAttack";
            case ActionType::ReturnToOutpost:
                return "ReturnToOupost";
            case ActionType::ApproachNearestEnemy:
                return "ApproachNearestEnemy";
            case ActionType::TargetNextEnemy:
                return "TargetNextEnemy";
            case ActionType::TargetPreviousEnemy:
                return "TargetPreviousEnemy";
            case ActionType::TargetStrongestEnemyWithinDistance:
                return "TargetStrongestEnemyWithinDistance";
            case ActionType::TargetNearestEnemyWithinDistance:
                return "TargetNearestEnemyWithinDistance";
            case ActionType::EnterPortal:
                return "EnterPortal";
        }
        return "Unknown";
    }
}

void ActionQueuePlugin::DrawSettings()
{
    if (!toolbox_handle) {
        return;
    }

    std::filesystem::path scripts_dir;
    if (bot_files.empty() && ActionQueueUtils::PathGetDocumentsPath(scripts_dir, L"GWToolboxpp\\scripts")) {
        if (std::filesystem::exists(scripts_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(scripts_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".bot") {
                    bot_files.push_back(entry.path().filename().string());
                }
            }
        }
    }

    if (!bot_files.empty()) {
        if (ImGui::BeginCombo("Available .bot scripts", selected_bot_index >= 0 ? bot_files[selected_bot_index].c_str() : "Select script")) {
            for (size_t i = 0; i < bot_files.size(); ++i) {
                const bool is_selected = (selected_bot_index == static_cast<int>(i));
                if (ImGui::Selectable(bot_files[i].c_str(), is_selected)) {
                    selected_bot_index = static_cast<int>(i);
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Run selected script") && selected_bot_index >= 0) {
            std::filesystem::path doc_path;
            if (ActionQueueUtils::PathGetDocumentsPath(doc_path, L"GWToolboxpp\\scripts")) {
                std::filesystem::path full_script = doc_path / bot_files[selected_bot_index];
                if (std::filesystem::exists(full_script)) {
                    current_script = full_script.string(); // <-- set current_script
                    LoadScriptFromFile(current_script);
                }
                else {
                    // printf("[ScriptLoader] Selected script not found on disk.\n");
                }
            }
        }
    }

    else {
        ImGui::Text("No .bot scripts found in GWToolboxpp\\scripts");
    }

    ImGui::Checkbox("Repeat script after completion", &repeat_script);

    // Status Display
    GW::Agent* player = GW::Agents::GetObservingAgent();
    GW::AgentLiving* player_living = player ? player->GetAsAgentLiving() : nullptr;

    if (player_living) {
        ImGui::Text("Player state:");
        ImGui::Text("Casting: %s", player_living->GetIsCasting() ? "true" : "false");
        ImGui::Text("Moving: %s", player_living->GetIsMoving() ? "true" : "false");
        ImGui::Text("Idle:   %s", player_living->GetIsIdle() ? "true" : "false");
        ImGui::Text("Attacking:   %s", player_living->GetIsAttacking() ? "true" : "false");

        // NEW: Display the plugin's casting state (only if action is available)
        if (!action_queue.empty()) {
            const auto& action = action_queue.front();
            ImGui::Text("Casting (Hook-tracked):  %s", action.cast_in_progress ? "true" : "false");
        }
        else {
            ImGui::Text("Casting (Hook-tracked):  (no action)");
        }

        // Distance to target
        GW::AgentID target_id = GW::Agents::GetTargetId();
        GW::Agent* target = GW::Agents::GetAgentByID(target_id);
        if (target) {
            float distance = GW::GetDistance(player->pos, target->pos);
            ImGui::Text("Distance to Target: %.2f", distance);
        }
        else {
            ImGui::Text("Distance to Target: (no target)");
        }
    }
    else {
        ImGui::Text("PlayerLiving not available.");
    }

    // --- display current queue action ---
    if (!action_queue.empty()) {
        const auto& action = action_queue.front();
        ImGui::Text("Current Action: %s", ActionTypeToString(action.type));
    }
    else {
        ImGui::Text("Current Action: (none)");
    }

    const GW::Agent* agent = GW::Agents::GetObservingAgent();
    if (agent) {
        ImGui::Text("Your position:");
        ImGui::Text("X: %.2f", agent->pos.x);
        ImGui::Text("Y: %.2f", agent->pos.y);
    }
    else {
        ImGui::Text("Agent not available.");
    }
}

void ActionQueuePlugin::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    ToolboxPlugin::Initialize(ctx, allocator_fns, toolbox_dll);

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::MessageCore>(&chat_error_hook_entry, [this](GW::HookStatus* status, GW::Packet::StoC::MessageCore* packet) {
        HandleCastFailMessage(status, packet);
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValue>(
    &hook_generic_value,
    [this](GW::HookStatus* status, GW::Packet::StoC::GenericValue* packet) {
        this->OnGenericValuePacket(status, packet);
    },
    0x8000
);

}

void ActionQueuePlugin::SignalTerminate()
{
    ToolboxPlugin::SignalTerminate();
    GW::StoC::RemoveCallback<GW::Packet::StoC::GenericValue>(&hook_generic_value);
    GW::StoC::RemoveCallback<GW::Packet::StoC::MessageCore>(&chat_error_hook_entry);
    GW::Chat::DeleteCommand(&ChatCmd_HookEntry);
}

bool ActionQueuePlugin::CanTerminate() {
    return true;
}

void ActionQueuePlugin::Draw(IDirect3DDevice9*)
{
    if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Loading) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if (ImGui::Begin(Name(), GetVisiblePtr(), ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::Text("Example plugin: area loading...");
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void ActionQueuePlugin::Update(float /*delta*/)
{
    if (action_queue.empty()) {
        if (repeat_script && !current_script.empty() && !was_empty) {
            last_finished = std::chrono::steady_clock::now();
            was_empty = true;
            return;
        }

        if (repeat_script && was_empty) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_finished).count();
            if (elapsed >= 1) { // Wait 1 seconds before repeating
                LoadScriptFromFile(current_script);
                was_empty = false;
            }
        }

        return; // Always return if queue is empty
    }

    was_empty = false;

    auto player = GW::Agents::GetObservingAgent();
    if (!player) return;

    auto& action = action_queue.front();
    uint64_t now = GetTickCount64();

    if (action.started_timestamp == 0) {
        action.started_timestamp = now;
    }

    auto living = player->GetAsAgentLiving();
    if (living && living->hp == 0) {
        //printf("[ActionQueue] Player is dead — clearing queue and returning to outpost\n");
        action_queue = {};
        QueueReturnToOutpost();
        return;
    }

    const uint64_t TIMEOUT_MS = 120000; // 2 minutes
    if (now - action.started_timestamp > TIMEOUT_MS) {
        //printf("[ActionQueue] Action timed out — clearing queue and returning to outpost\n");
        action_queue = {};
        QueueReturnToOutpost();
        return;
    }

    switch (action.type) {

    case ActionType::Move: {
            if (!action.is_moving) {
                GW::GameThread::Enqueue([x = action.x, y = action.y] {
                    GW::Agents::Move(x, y);
                });
                action.is_moving = true;
            }

            //auto living = player->GetAsAgentLiving();
            if (living) {
                // Transition-based idle check (simplified)
                if (!action.was_not_idle && !living->GetIsIdle()) {
                    action.was_not_idle = true;
                }
                else if (action.was_not_idle && living->GetIsIdle()) {
                    action_queue.pop();
                }
            }
            return;
        }

        case ActionType::ReturnToOutpost: {
            if (!action.entered) {
                GW::Chat::SendChat('/', L"resign");
                action.entered = true;
            }
            if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) {
                action_queue.pop();
            }
            return;
        }

        case ActionType::EnterChallenge: {
            if (!action.entered) {
                GW::Map::EnterChallenge();
                action.entered = true;
            }

            // Attempt to auto-confirm the "Yes" button if it appears
            GW::UI::Frame* party_frame = GW::UI::GetFrameByLabel(L"Party");
            if (party_frame) {
                GW::UI::Frame* yes_button = GW::UI::GetChildFrame(party_frame, 1, 11, 6); // Confirm offsets!

                if (yes_button && yes_button->IsVisible() && yes_button->IsCreated()) {
                    GW::UI::ButtonClick(yes_button);
                    //printf("[ActionQueue] Clicked 'Yes' button on EnterChallenge.\n");
                }
             }

             // Finish the action when the map actually changes
             if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Explorable) {
                 action_queue.pop();
             }
                return;
        }

        case ActionType::EnterPortal: {
            if (!action.is_moving) {
                GW::GameThread::Enqueue([x = action.x, y = action.y] {
                    GW::Agents::Move(x, y);
                });
                action.is_moving = true;
            }

            // Confirm if the instance has changed
            if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Explorable) {
                //printf("[ActionQueue] Entered explorable area, popping EnterPortal action\n");
                action_queue.pop();
            }

            return;
        }

        case ActionType::InteractAgent: {
            // If agent_id not set (== 0), fall back to current target
            if (action.agent_id == 0) {
                action.agent_id = GW::Agents::GetTargetId();
                if (action.agent_id == 0) {
                    //printf("[InteractAgent] No agent target selected, skipping.\n");
                    action_queue.pop();
                    return;
                }
            }

            auto target = GW::Agents::GetAgentByID(action.agent_id);

            if (!living || !target) {
                //printf("[InteractAgent] Invalid target or player not found.\n");
                action_queue.pop();
                return;
            }

            if (!action.is_interacting) {
                GW::GameThread::Enqueue([target_id = action.agent_id] {
                    if (auto tgt = GW::Agents::GetAgentByID(target_id)) {
                        //printf("[InteractAgent] Interacting with agent ID: %d\n", target_id);
                        GW::Agents::InteractAgent(tgt);
                    }
                });
                action.is_interacting = true;
            }

            // Transition-based idle check
            if (!action.was_not_idle && !living->GetIsIdle()) {
                action.was_not_idle = true;
            }
            else if (action.was_not_idle && (living->GetIsIdle() || living->GetIsAttacking())) {
                //printf("[InteractAgent] Interaction complete.\n");
                action_queue.pop();
            }

            return;
        }

        case ActionType::DropItem:
            GW::GameThread::Enqueue([] {
                GW::UI::Keypress(GW::UI::ControlAction::ControlAction_DropItem);
            });
            action_queue.pop();
            return;

        case ActionType::Wait: {
            if (!action.wait_initialized) {
                action.wait_until = now + action.wait_until; // duration was pre-stored
                action.wait_initialized = true;
            }
            if (now >= action.wait_until) {
                action_queue.pop();
            }
            return;
        }

        case ActionType::UseSkill: {
            auto skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
            if (!skillbar || !skillbar->IsValid()) return;

            int slot = GW::SkillbarMgr::GetSkillSlot(static_cast<GW::Constants::SkillID>(action.skill_id));
            if (slot < 0) {
                action_queue.pop();
                return;
            }

            const GW::SkillbarSkill& skill = skillbar->skills[slot];

            if (action.cast_failed) {
                //printf("[ActionQueue] Skill cast failed, skipping action\n");
                action.cast_in_progress = false;
                action_queue.pop();
                return;
            }

            if (!action.skill_triggered && skill.recharge == 0) {
                action.skill_triggered = true;
                action.cast_in_progress = true;

                GW::GameThread::Enqueue([sid = action.skill_id, tid = action.skill_target_id] {
                    GW::SkillbarMgr::UseSkillByID(sid, tid);
                });
                return;
            }

            if (action.skill_triggered && !action.cast_in_progress) {

                if (living && (living->GetIsIdle() || living->GetIsAttacking())) {
                    //printf("[ActionQueue] Cast complete and player idle/attacking, popping\n");
                    action_queue.pop();
                }
            }
            return;
        }

        case ActionType::WaitForIdle: {

            if (!living) {
                action_queue.pop();
                return;
            }
            if (!action.was_not_idle && !living->GetIsIdle()) {
                action.was_not_idle = true;
            }
            else if (action.was_not_idle && living->GetIsIdle()) {
                action_queue.pop();
            }
            return;
        }

        case ActionType::InteractNearestItemWithinDistance: {
            //auto living = player ? player->GetAsAgentLiving() : nullptr;
            if (!living) {
                action_queue.pop();
                return;
            }

            if (!action.item_targeted) {
                GW::GameThread::Enqueue([] {
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_TargetNearestItem);
                });
                action.item_targeted = true;
                return;
            }

            GW::AgentID target_id = GW::Agents::GetTargetId();
            GW::Agent* target = GW::Agents::GetAgentByID(target_id);
            if (!target) {
                action_queue.pop(); // Nothing targeted
                return;
            }

            float distance = GW::GetDistance(player->pos, target->pos);
            if (distance > action.max_item_distance) {
                action_queue.pop(); // Too far
                return;
            }

            if (!action.item_interacted) {
                GW::GameThread::Enqueue([] {
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                });
                action.item_interacted = true;
                return;
            }

            // Now do the transition-based idle check
            if (!action.was_not_idle && !living->GetIsIdle()) {
                action.was_not_idle = true;
            }
            else if (action.was_not_idle && living->GetIsIdle()) {
                action_queue.pop(); // Done
            }
            // else: wait another frame
            return;
        }

        case ActionType::TargetNearestEnemyAndAttack: {

            if (!living) {
                action_queue.pop(); // Failsafe
                return;
            }

            if (!action.skill_triggered) {
                // Trigger targeting and interaction only once
                GW::GameThread::Enqueue([] {
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_TargetNearestEnemy);
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                });
                action.skill_triggered = true;
                return; // wait for next frame to begin monitoring
            }

            if (living->GetIsAttacking()) {
                action_queue.pop(); 
            }
            // else: waiting for attack to start or finish
            return;
        }

        case ActionType::ApproachNearestEnemy: {

            if (!living) {
                action_queue.pop(); // Failsafe
                return;
            }

            if (!action.skill_triggered) {
                GW::GameThread::Enqueue([] {
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_TargetNearestEnemy);
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                });
                action.skill_triggered = true;
                return; // wait until next frame
            }

            const GW::Agent* target = GW::Agents::GetTarget();
            if (!target) {
                // Still no target; keep trying for a bit or give up
                return;
            }

            float distance = GW::GetDistance(player->pos, target->pos);
            if (distance <= action.approach_distance || living->GetIsAttacking()) {
                GW::GameThread::Enqueue([] {
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_CancelAction);
                });
                action_queue.pop(); // Finished approaching
            }
            return;
        }

        case ActionType::TargetNextEnemy:
        case ActionType::TargetPreviousEnemy: {
            if (!action.entered) {
                action.entered = true;
                const auto ctrl_action = action.type == ActionType::TargetNextEnemy ? GW::UI::ControlAction::ControlAction_TargetNextEnemy : GW::UI::ControlAction::ControlAction_TargetPreviousEnemy;

                GW::GameThread::Enqueue([ctrl_action] {
                    GW::UI::Keypress(ctrl_action);
                });
            }
            action_queue.pop();
            return;
        }

        case ActionType::TargetStrongestEnemyWithinDistance: {

            if (!living) {
                action_queue.pop(); // Failsafe
                return;
            }

            GW::Agent* best_target = nullptr;
            float highest_percent_hp = -1.0f;

            GW::AgentArray* agents_ptr = GW::Agents::GetAgentArray();
            if (!agents_ptr) return;

            for (GW::Agent* agent : *agents_ptr) {
                if (!agent) continue;

                GW::AgentLiving* target_living = agent->GetAsAgentLiving();
                if (!target_living || target_living->allegiance != GW::Constants::Allegiance::Enemy) continue;

                float distance = GW::GetDistance(player->pos, target_living->pos);
                if (distance > action.approach_distance) continue;

                float hp_percent = target_living->hp; // Already 0.0 to 1.0
                if (hp_percent > highest_percent_hp) {
                    highest_percent_hp = hp_percent;
                    best_target = agent;
                }
            }

            if (best_target) {
                GW::Agents::ChangeTarget(best_target);
            }

            action_queue.pop();
            return;
        }
        
        case ActionType::TargetNearestEnemyWithinDistance: {

            if (!living) {
                action_queue.pop(); // Failsafe
                return;
            }

            // Search for nearest enemy within approach distance
            GW::AgentArray* agents_ptr = GW::Agents::GetAgentArray();
            if (!agents_ptr) return;

            GW::Agent* nearest = nullptr;
            float min_dist = std::numeric_limits<float>::max();

            for (GW::Agent* agent : *agents_ptr) {
                if (!agent) continue;
                GW::AgentLiving* target_living = agent->GetAsAgentLiving();
                if (!target_living || target_living->allegiance != GW::Constants::Allegiance::Enemy) continue;

                float dist = GW::GetDistance(player->pos, target_living->pos);
                if (dist <= action.approach_distance && dist < min_dist) {
                    min_dist = dist;
                    nearest = agent;
                }
            }

            if (nearest) {
                GW::Agents::ChangeTarget(nearest);
            }

            action_queue.pop();
            return;
        }

        default:
            // unknown action → just pop it so we don’t get stuck
            action_queue.pop();
            return;
    }
}

void ActionQueuePlugin::QueueMove(float x, float y)
{
    action_queue.emplace(ActionType::Move, x, y);
}

void ActionQueuePlugin::QueueReturnToOutpost()
{;
    action_queue.emplace(std::move(ActionType::ReturnToOutpost));
}

void ActionQueuePlugin::QueueEnterChallenge()
{
    action_queue.emplace(ActionType::EnterChallenge);
}

void ActionQueuePlugin::QueueInteract(uint32_t agent_id)
{
    action_queue.emplace(ActionType::InteractAgent, agent_id);
}

void ActionQueuePlugin::QueueEnterPortal(float x, float y)
{
    action_queue.emplace(ActionType::EnterPortal, x, y);
}

void ActionQueuePlugin::QueueDropItem()
{
    action_queue.emplace(ActionType::DropItem);
}

void ActionQueuePlugin::QueueWait(uint64_t duration_ms)
{
    action_queue.emplace(QueuedAction::MakeWait(duration_ms));
}

void ActionQueuePlugin::QueueUseSkill(uint32_t skill_id, uint32_t target_id)
{
    action_queue.emplace(QueuedAction::MakeUseSkill(skill_id, target_id));
}

void ActionQueuePlugin::QueueWaitForIdle()
{
    action_queue.emplace(ActionType::WaitForIdle);
}

void ActionQueuePlugin::QueueInteractNearestItemWithinDistance(float max_distance)
{
    action_queue.emplace(QueuedAction::MakeInteractNearestItemWithinDistance(max_distance));
}

void ActionQueuePlugin::QueueTargetNearestEnemyAndAttack()
{
    action_queue.emplace(ActionType::TargetNearestEnemyAndAttack);
}

void ActionQueuePlugin::QueueApproachNearestEnemy(float within_distance)
{
    action_queue.emplace(QueuedAction::MakeApproachNearestEnemy(within_distance));
}

void ActionQueuePlugin::QueueTargetNextEnemy()
{
    action_queue.emplace(QueuedAction::MakeTargetNextEnemy());
}

void ActionQueuePlugin::QueueTargetPreviousEnemy()
{
    action_queue.emplace(QueuedAction::MakeTargetPreviousEnemy());
}

void ActionQueuePlugin::QueueTargetStrongestEnemyWithinDistance(float within_distance)
{
    action_queue.emplace(QueuedAction::MakeTargetStrongestEnemy(within_distance));
}

void ActionQueuePlugin::QueueTargetNearestEnemyWithinDistance(float within_distance)
{
    action_queue.emplace(QueuedAction::MakeTargetNearestEnemyWithinDistance(within_distance));
}

void ActionQueuePlugin::OnGenericValuePacket(GW::HookStatus*, GW::Packet::StoC::GenericValue* packet)
{
    using namespace GW::Packet::StoC::GenericValueID;

    GW::Agent* player = GW::Agents::GetControlledCharacter();
    if (!player || packet->agent_id != player->agent_id) return; // Ignore if not the player

    if (action_queue.empty()) return;
    auto& current_action = action_queue.front(); // 🔄 Get the current action

    //printf("[GenericValue] Received ID: %u (Player)\n", packet->value_id);

    switch (packet->value_id) {
        case skill_activated:
        //case instant_skill_activated:
        case attack_skill_activated:
        case attack_started:
            //current_action.cast_in_progress = true;
            //printf("Agent started casting\n");
            break;
        case skill_finished:
        case skill_stopped:
        case instant_skill_activated:
        //case attack_stopped:
        case attack_skill_finished:
        case attack_skill_stopped:
        case interrupted:
            current_action.cast_in_progress = false;
            //printf("Agent stopped casting\n");
            break;
    }
}

void ActionQueuePlugin::HandleCastFailMessage(GW::HookStatus*, GW::Packet::StoC::MessageCore* packet)
{
    const wchar_t* msg = packet->message;
    if (!msg || action_queue.empty()) return;

    auto& current_action = action_queue.front();

    switch (msg[0]) {
        case 0x8A9:
            //printf("[CAST FAIL] Not enough energy\n");
            break;
        case 0x8C2:
            //printf("[CAST FAIL] Invalid spell target\n");
            break;
        case 0x8C3:
            //printf("[CAST FAIL] Target out of range\n");
            break;
        //case 0x8C4: <- don't fail on recharge, just wait
            //printf("[CAST FAIL] Skill recharging\n");
            //break;
        case 0x8AB:
            //printf("[CAST FAIL] View obstructed\n");
            break;
        default:
            return;
    }

    current_action.cast_failed = true;
}

void ActionQueuePlugin::LoadScriptFromFile(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file) {
        //printf("[ActionQueue] Failed to open script: %s\n", filename.c_str());
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string command;
        iss >> command;

        if (command == "EnterPortal") {
            float x, y;
            iss >> x >> y;
            QueueEnterPortal(x, y);
        }
        else if (command == "Move") {
            float x, y;
            iss >> x >> y;
            QueueMove(x, y);
        }
        else if (command == "UseSkill") {
            uint32_t skill_id, target_id = 0;
            iss >> skill_id;
            if (iss >> target_id) {
                QueueUseSkill(skill_id, target_id);
            }
            else {
                QueueUseSkill(skill_id, 0);
            }
        }
        else if (command == "ApproachNearestEnemy") {
            float dist;
            iss >> dist;
            QueueApproachNearestEnemy(dist);
        }
        else if (command == "Wait") {
            uint64_t ms;
            iss >> ms;
            QueueWait(ms);
        }
        else if (command == "InteractNearestItemWithinDistance") {
            float max_dist;
            iss >> max_dist;
            QueueInteractNearestItemWithinDistance(max_dist);
        }
        else if (command == "ReturnToOutpost") {
            QueueReturnToOutpost();
        }
        else if (command == "EnterChallenge") {
            QueueEnterChallenge();
        }
        else if (command == "DropItem") {
            QueueDropItem();
        }
        else if (command == "Interact") {
            uint32_t agent_id;
            iss >> agent_id;
            QueueInteract(agent_id);
        }
        else if (command == "TargetNearestEnemyWithinDistance") {
            float dist;
            iss >> dist;
            QueueTargetNearestEnemyWithinDistance(dist);
        }
        else if (command == "TargetStrongestEnemyWithinDistance") {
            float dist;
            iss >> dist;
            QueueTargetStrongestEnemyWithinDistance(dist);
        }
        else {
            //printf("[ActionQueue] Unknown command: %s\n", command.c_str());
        }
    }
}

bool ActionQueueUtils::PathGetDocumentsPath(std::filesystem::path& out, const wchar_t* suffix)
{
    wchar_t temp[MAX_PATH];
    const HRESULT result = SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, temp);

    if (FAILED(result)) {
        fwprintf(stderr, L"%S: SHGetFolderPathW failed (HRESULT:0x%lX)\n", __func__, result);
        return false;
    }
    if (!temp[0]) {
        fwprintf(stderr, L"%S: SHGetFolderPathW returned empty path\n", __func__);
        return false;
    }
    std::filesystem::path p = temp;
    if (suffix && suffix[0]) {
        p = p.append(suffix);
    }
    out.assign(p);
    return true;
}
