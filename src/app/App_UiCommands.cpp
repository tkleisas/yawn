// App_UiCommands.cpp — env-gated TCP UI command channel.
//
// Enabled by setting YAWN_CMD in the environment before launch:
//   YAWN_CMD=8765 ./YAWN        # listen on 127.0.0.1:8765
//   YAWN_CMD=1 ./YAWN           # ephemeral port (logged at startup)
//
// Scripts then drive the UI semantically (menu / menuitem / addtrack /
// addinstrument / key / click / shot / …) over a line-based protocol
// instead of synthesizing X11 input — see scripts/ui_probe.py for a
// reference client. Every line gets an "OK …" or "ERR …" ack; `shot`
// is the only deferred reply (it acks after the next frame renders).
#include "app/App.h"
#include "util/TcpLineServer.h"
#include "util/Logger.h"
#include "ui/framework/v2/ContextMenu.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace yawn {

namespace {

// Split a command line into tokens. Space-separated; double quotes
// group tokens containing spaces (addinstrument 1 "FM Synth").
std::vector<std::string> splitArgs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    bool has = false;
    for (char c : line) {
        if (c == '"') {
            quoted = !quoted;
            has = true;   // "" is a valid (empty) token
        } else if (c == ' ' && !quoted) {
            if (has) { out.push_back(cur); cur.clear(); has = false; }
        } else {
            cur += c;
            has = true;
        }
    }
    if (has) out.push_back(cur);
    return out;
}

// Rejoin tokens [start..end) with single spaces — device / menu /
// item names may contain spaces whether or not the client quoted them.
std::string joinArgs(const std::vector<std::string>& args, size_t start) {
    std::string out;
    for (size_t i = start; i < args.size(); ++i) {
        if (!out.empty()) out += ' ';
        out += args[i];
    }
    return out;
}

bool parseFloat(const std::string& s, float& out) {
    try {
        size_t used = 0;
        out = std::stof(s, &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

} // namespace

void App::initUiCommandServer() {
    const char* env = std::getenv("YAWN_CMD");
    if (!env) return;
    // Empty or "1" means "any free port" — the bound port is logged
    // so the launching script can read it back.
    int port = 0;
    const std::string v = env;
    if (!v.empty() && v != "1") port = std::atoi(v.c_str());

    auto server = std::make_unique<util::TcpLineServer>(port);
    if (!server->ok()) {
        LOG_ERROR("UiCmd", "YAWN_CMD set but bind on 127.0.0.1:%d failed — "
                           "UI command channel disabled", port);
        return;
    }
    server->setLineHandler([this](int clientId, const std::string& line) {
        m_uiCmdClientId = clientId;   // lets `shot` ack the right client
        return executeUiCommand(line);
    });
    LOG_INFO("UiCmd", "UI command channel listening on 127.0.0.1:%d",
             server->port());
    m_uiCmdServer = std::move(server);
}

void App::pumpUiCommands() {
    if (m_uiCmdServer) m_uiCmdServer->pump();
}

std::string App::executeUiCommand(const std::string& line) {
    const std::vector<std::string> args = splitArgs(line);
    if (args.empty()) return "ERR empty command";
    const std::string& verb = args[0];

    if (verb == "ping") return "OK pong";

    if (verb == "shot") {
        if (args.size() < 2) return "ERR usage: shot <path.png>";
        // Deferred ack: the capture happens at the end of this frame's
        // render() (back buffer complete, pre-swap), which then queues
        // "OK shot" back to this client.
        m_pendingShotPath = args[1];
        m_pendingShotClient = m_uiCmdClientId;
        return std::string();
    }

    if (verb == "key") {
        if (args.size() < 2) return "ERR usage: key <name>";
        const SDL_Keycode kc = SDL_GetKeyFromName(args[1].c_str());
        if (kc == SDLK_UNKNOWN) return "ERR unknown key: " + args[1];
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.windowID = SDL_GetWindowID(m_mainWindow.getHandle());
        ev.key.key = kc;
        ev.key.mod = SDL_KMOD_NONE;
        ev.key.down = true;
        ev.key.repeat = false;
        SDL_PushEvent(&ev);
        ev.type = SDL_EVENT_KEY_UP;
        ev.key.down = false;
        SDL_PushEvent(&ev);
        return "OK key";
    }

    if (verb == "click" || verb == "rclick" || verb == "dclick" ||
        verb == "mousemove") {
        if (args.size() < 3)
            return "ERR usage: " + verb + " <x> <y>";
        float x = 0, y = 0;
        if (!parseFloat(args[1], x) || !parseFloat(args[2], y))
            return "ERR bad coordinates";
        const Uint8 button = (verb == "rclick") ? SDL_BUTTON_RIGHT
                                                : SDL_BUTTON_LEFT;
        const int cycles = (verb == "dclick") ? 2 : 1;

        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.windowID = SDL_GetWindowID(m_mainWindow.getHandle());
        ev.motion.x = x;
        ev.motion.y = y;
        SDL_PushEvent(&ev);
        if (verb == "mousemove") return "OK mousemove";

        for (int i = 0; i < cycles; ++i) {
            ev = SDL_Event{};
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.windowID = SDL_GetWindowID(m_mainWindow.getHandle());
            ev.button.button = button;
            ev.button.down = true;
            ev.button.clicks = static_cast<Uint8>(i + 1);
            ev.button.x = x;
            ev.button.y = y;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.down = false;
            SDL_PushEvent(&ev);
        }
        return "OK " + verb;
    }

    if (verb == "view") {
        if (args.size() < 2) return "ERR usage: view session|arrangement";
        if (args[1] == "session") {
            switchToView(ViewMode::Session);
            return "OK view";
        }
        if (args[1] == "arrangement") {
            switchToView(ViewMode::Arrangement);
            return "OK view";
        }
        return "ERR usage: view session|arrangement";
    }

    if (verb == "menu") {
        if (args.size() < 2) return "ERR usage: menu <title>";
        if (m_menuBar.openMenuByTitle(joinArgs(args, 1)))
            return "OK menu";
        return "ERR no such menu";
    }

    if (verb == "menuitem") {
        if (args.size() < 2) return "ERR usage: menuitem <label>";
        auto& mgr = ui::fw2::ContextMenuManager::instance();
        if (!mgr.isOpen()) return "ERR no open menu";
        if (mgr.activateItemByLabel(joinArgs(args, 1)))
            return "OK menuitem";
        return "ERR no such item";
    }

    if (verb == "addtrack") {
        if (args.size() < 2) return "ERR usage: addtrack audio|midi|visual";
        if (args[1] == "audio")  { addTrackOfType(Track::Type::Audio);  return "OK addtrack"; }
        if (args[1] == "midi")   { addTrackOfType(Track::Type::Midi);   return "OK addtrack"; }
        if (args[1] == "visual") { addTrackOfType(Track::Type::Visual); return "OK addtrack"; }
        return "ERR usage: addtrack audio|midi|visual";
    }

    if (verb == "addinstrument") {
        if (args.size() < 3) return "ERR usage: addinstrument <trackIndex> <name>";
        int track = -1;
        try { track = std::stoi(args[1]); } catch (...) {}
        if (track < 0 || track >= m_project.numTracks()) return "ERR bad track";
        if (!addInstrumentToTrack(track, joinArgs(args, 2)))
            return "ERR unknown device";
        return "OK addinstrument";
    }

    if (verb == "addeffect") {
        if (args.size() < 3) return "ERR usage: addeffect <trackIndex> <name>";
        int track = -1;
        try { track = std::stoi(args[1]); } catch (...) {}
        if (track < 0 || track >= m_project.numTracks()) return "ERR bad track";
        if (!addAudioEffectToTrack(track, joinArgs(args, 2)))
            return "ERR unknown device";
        return "OK addeffect";
    }

    if (verb == "setparam") {
        if (args.size() < 4)
            return "ERR usage: setparam <trackIndex> <paramIndex> <value>";
        int track = -1, param = -1;
        float value = 0.0f;
        try { track = std::stoi(args[1]); } catch (...) {}
        try { param = std::stoi(args[2]); } catch (...) {}
        if (track < 0 || track >= m_project.numTracks()) return "ERR bad track";
        auto* inst = m_audioEngine.instrument(track);
        if (!inst) return "ERR no instrument on track";
        if (param < 0 || param >= inst->parameterCount()) return "ERR bad param";
        if (!parseFloat(args[3], value)) return "ERR bad value";
        // Same path as the detail-panel knob (DetailPanelWidget.h).
        inst->setParameter(param, value);
        return "OK setparam";
    }

    if (verb == "dialog") {
        if (args.size() < 2) return "ERR usage: dialog preferences|about|shortcuts|export";
        if (args[1] == "preferences") { openPreferencesDialog();        return "OK dialog"; }
        if (args[1] == "about")       { showAboutDialog();              return "OK dialog"; }
        if (args[1] == "shortcuts")   { showKeyboardShortcutsDialog();  return "OK dialog"; }
        if (args[1] == "export")      { openExportDialog();             return "OK dialog"; }
        return "ERR usage: dialog preferences|about|shortcuts|export";
    }

    if (verb == "dbg") {
        // Diagnostic dump for input-dispatch bugs: what's floating on
        // the LayerStack, and who holds fw2 mouse capture.
        std::ostringstream oss;
        m_fw2LayerStack.dumpState(oss);
        LOG_INFO("UiCmd", "dbg LayerStack (entries=%d):\n%s",
                 m_fw2LayerStack.totalEntryCount(), oss.str().c_str());
        LOG_INFO("UiCmd", "dbg capturedWidget=%p menuBarOpen=%d ctxMenuOpen=%d menuOpenIdx=%d",
                 static_cast<void*>(ui::fw2::Widget::capturedWidget()),
                 m_menuBar.isOpen() ? 1 : 0,
                 ui::fw2::ContextMenuManager::instance().isOpen() ? 1 : 0,
                 m_menuBar.openIndex());
        return "OK dbg";
    }

    if (verb == "quit") {
        // Same path as File → Quit: the run loop exits at the end of
        // this frame (after the ack below is flushed).
        m_running = false;
        return "OK quit";
    }

    return "ERR unknown verb: " + verb;
}

} // namespace yawn
