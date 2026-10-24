// The AI Activity panel and the approval prompt. Approvals, pausing and disconnecting call the RpcServer
// directly instead of running commands: no client may approve its own requests.

#include "Panels.hpp"

#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/Icons.hpp>

#include <cstdio>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr usize kFeedCapacity = 256;

int permissionIndex(std::string_view mode) noexcept
{
    return mode == "read-only" ? 0 : mode == "auto" ? 2 : 1;
}

} // namespace

struct Agents::State
{
    u64 version = 0;
    std::vector<rpc::ClientInfo> clients;
    std::vector<rpc::ActivityEntry> feed; // oldest first
    std::vector<u64> undoable;            // sequence of each client's newest undoable change
    u64 lastSequence = 0;
    u64 lastApproval = 0;

    State()
    {
        feed.reserve(kFeedCapacity * 2);
        clients.reserve(8);
    }

    void drawApproval(Impl& shell, const rpc::PendingApproval& approval, std::string_view id);
};

void Agents::State::drawApproval(Impl& shell, const rpc::PendingApproval& approval, std::string_view id)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    {
        const ScopedFont font(shell.options.fonts, ui::FontRole::Strong);
        ImGui::TextColored(imvec(shell.theme.color(ui::ThemeToken::Warning)), "%s",
                           ui::icons::kWarning.c_str());
        ImGui::SameLine();
        ImGui::TextWrapped("%s", text.format(s::kAgentsApproval,
                                             {{"client", approval.client}, {"method", approval.method}}));
    }
    ImGui::TextWrapped("%s", text.format(s::kAgentsApprovalPermission,
                                         {{"permission", permissionClassName(approval.permission)}}));
    {
        const ScopedFont font(shell.options.fonts, ui::FontRole::Mono);
        ImGui::PushStyleColor(ImGuiCol_Text, imvec(shell.theme.color(ui::ThemeToken::TextSecondary)));
        ImGui::TextWrapped("%s", text.copy(approval.params));
        ImGui::PopStyleColor();
    }
    rpc::RpcServer& server = *shell.agents;
    if (ui::primaryButton(kit, text.label(s::kAgentsAllowOnce, text.concat(id, ".allow_once"))))
    {
        (void)server.decide(approval.id, rpc::ApprovalDecision::AllowOnce);
    }
    ImGui::SameLine();
    if (ImGui::Button(text.label(s::kAgentsAllowSession, text.concat(id, ".allow_session"))))
    {
        (void)server.decide(approval.id, rpc::ApprovalDecision::AllowForSession);
    }
    ImGui::SameLine();
    if (ImGui::Button(text.label(s::kAgentsDeny, text.concat(id, ".deny"))))
    {
        (void)server.decide(approval.id, rpc::ApprovalDecision::Deny);
    }
}

Agents::Agents()
    : m_state(std::make_unique<State>())
{
}

Agents::~Agents() = default;

usize Agents::clientCount() const noexcept
{
    return m_state->clients.size();
}

void Agents::update(Impl& shell)
{
    State& state = *m_state;
    if (shell.agents == nullptr)
    {
        state.clients.clear();
        return;
    }
    rpc::RpcServer& server = *shell.agents;
    if (server.stateVersion() == state.version)
    {
        return;
    }
    state.version = server.stateVersion();
    state.clients = server.clients();
    server.activity(state.lastSequence, state.feed);
    if (state.feed.size() > kFeedCapacity)
    {
        state.feed.erase(state.feed.begin(), state.feed.end() - static_cast<std::ptrdiff_t>(kFeedCapacity));
    }
    if (!state.feed.empty())
    {
        state.lastSequence = state.feed.back().sequence;
    }
    // The undo link belongs to each client's newest successful change.
    state.undoable.clear();
    for (const rpc::ClientInfo& client : state.clients)
    {
        for (auto it = state.feed.rbegin(); it != state.feed.rend(); ++it)
        {
            const CommandDescriptor* command = shell.editor.commands().find(it->method);
            if (it->connection == client.connection && it->code == 0 && command != nullptr &&
                command->info.flags.undoable)
            {
                state.undoable.push_back(it->sequence);
                break;
            }
        }
    }
    for (const rpc::PendingApproval& approval : server.approvals())
    {
        if (approval.id > state.lastApproval)
        {
            state.lastApproval = approval.id;
            shell.toast(UiNotice::Warning,
                        shell.text.format(s::kToastApproval, {{"client", approval.client}}),
                        shell.text.format(s::kToastView), kToastShowAgents);
        }
    }
}

void Agents::draw(Impl& shell)
{
    State& state = *m_state;
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;

    // The permission mode applies to every client (a preference).
    const char* const modes[] = {text.format(s::kPrefsPermissionReadOnly),
                                 text.format(s::kPrefsPermissionAsk), text.format(s::kPrefsPermissionAuto)};
    int mode = permissionIndex(shell.editor.preferences().agentPermissions);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text.format(s::kAgentsMode));
    ImGui::SameLine();
    if (ui::segmentedControl(kit, "agents.mode", modes, mode))
    {
        constexpr const char* kPatches[] = {R"({"agentPermissions":"read-only"})",
                                            R"({"agentPermissions":"ask"})",
                                            R"({"agentPermissions":"auto"})"};
        shell.setPreferences(kPatches[mode]);
    }
    if (shell.agents == nullptr || state.clients.empty())
    {
        ui::emptyState(kit, ui::icons::kSmartToy, text.format(s::kAgentsEmptyTitle),
                       text.format(s::kAgentsEmptyHint));
        return;
    }
    rpc::RpcServer& server = *shell.agents;

    const std::span<const rpc::PendingApproval> approvals = server.approvals();
    if (!approvals.empty())
    {
        ui::sectionHeader(kit, text.format(s::kAgentsApprovals));
        for (usize i = 0; i < approvals.size(); ++i)
        {
            char id[48];
            std::snprintf(id, sizeof(id), "agents.approval.%zu", i);
            ImGui::PushID(static_cast<int>(i));
            state.drawApproval(shell, approvals[i], id);
            ImGui::PopID();
            ImGui::Separator();
        }
    }

    if (ImGui::BeginTable(text.hidden("agents.clients"), 4,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        setupStretchColumns({3.0f, 2.0f, 2.0f, 1.2f});
        for (const rpc::ClientInfo& client : state.clients)
        {
            ImGui::PushID(static_cast<int>(client.connection));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(imvec(shell.theme.color(ui::ThemeToken::Accent)), "%s",
                               ui::icons::kSmartToy.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(text.copy(client.client));
            if (!client.agent.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s %s", text.copy(client.agent), text.copy(client.agentVersion));
            }
            ImGui::TableNextColumn();
            if (!client.era.empty())
            {
                ImGui::TextDisabled("%s", text.format(s::kAgentsEra, {{"era", client.era}}));
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(text.format(s::kAgentsRequests, {{"count", client.requests}}));
            if (client.paused)
            {
                ImGui::SameLine();
                ui::badge(kit, text.format(s::kAgentsPaused), ui::ThemeToken::Warning);
            }
            ImGui::TableNextColumn();
            const char* clientId = text.concat("agents.client.", client.client);
            if (ui::iconButton(kit, client.paused ? ui::icons::kPlayArrow : ui::icons::kPause,
                               text.concat(clientId, client.paused ? ".resume" : ".pause"),
                               text.format(client.paused ? s::kAgentsResume : s::kAgentsPause)))
            {
                (void)server.setPaused(client.connection, !client.paused);
            }
            ImGui::SameLine();
            if (ui::iconButton(kit, ui::icons::kLinkOff, text.concat(clientId, ".disconnect"),
                               text.format(s::kAgentsDisconnect)))
            {
                (void)server.disconnect(client.connection);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ui::sectionHeader(kit, text.format(s::kAgentsFeed));
    if (ImGui::BeginTable(text.hidden("agents.feed"), 5,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY))
    {
        setupStretchColumns({1.2f, 1.6f, 3.0f, 0.8f, 1.0f});
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.feed.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                // Newest first.
                const rpc::ActivityEntry& entry = state.feed[state.feed.size() - 1 - static_cast<usize>(row)];
                ImGui::PushID(static_cast<int>(entry.sequence));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text.copy(entry.client));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text.copy(entry.method));
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", text.copy(entry.params));
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", text.copy(entry.params));
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%.1f ms", static_cast<f64>(entry.micros) / 1000.0);
                ImGui::TableNextColumn();
                if (entry.code == 0)
                {
                    ImGui::TextColored(imvec(shell.theme.color(ui::ThemeToken::Success)), "%s",
                                       ui::icons::kCheck.c_str());
                }
                else
                {
                    ImGui::TextColored(imvec(shell.theme.color(ui::ThemeToken::Error)), "%s %d",
                                       ui::icons::kError.c_str(), entry.code);
                }
                if (std::find(state.undoable.begin(), state.undoable.end(), entry.sequence) !=
                    state.undoable.end())
                {
                    ImGui::SameLine();
                    char id[48];
                    std::snprintf(id, sizeof(id), "agents.feed.%llu.undo",
                                  static_cast<unsigned long long>(entry.sequence));
                    if (ImGui::SmallButton(text.label(s::kAgentsUndo, id)))
                    {
                        (void)shell.run("history.undo", R"({"scope":"mine"})", "agents",
                                        text.concat("rpc:", entry.client));
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void Agents::drawPrompt(Impl& shell)
{
    if (shell.agents == nullptr || shell.agents->approvals().empty() ||
        !shell.editor.preferences().approvalPopups)
    {
        return;
    }
    const rpc::PendingApproval& approval = shell.agents->approvals().front();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 16.0f * shell.scale,
                                   viewport->WorkPos.y + 16.0f * shell.scale),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(420.0f * shell.scale, 0.0f));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("###agents.prompt", nullptr, kFlags))
    {
        m_state->drawApproval(shell, approval, "agents.prompt");
    }
    ImGui::End();
}

} // namespace illusion::editor::shell::detail
