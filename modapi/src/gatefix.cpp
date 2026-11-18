#include "gatefix.h"
#include <imgui.h>
#include <windows.h>
#include <cstdio>
#include <ctime>
#include <string>

namespace {
    constexpr uintptr_t kStatus = 0x60AD6C;           // Status*: +0x160 station, +0x168 system
    constexpr uintptr_t kJumpgateVtable = 0x54B9B4;   // PlayerJumpgate vtable (PC)
    constexpr int kArrivalSlot = 2;

    template <typename T>
    bool rd(uintptr_t address, T& out)
    {
        SIZE_T done = 0;
        return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &out, sizeof(T), &done) && done == sizeof(T);
    }

    template <typename T>
    bool wr(uintptr_t address, T value)
    {
        SIZE_T done = 0;
        return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(T), &done) && done == sizeof(T);
    }

    struct Slot {
        uintptr_t obj = 0;
        uintptr_t vtable = 0;
        uintptr_t geom = 0;
        uint8_t visible = 0;   // KIPlayer +0xDD
        uint8_t g48 = 0;       // AEGeometry +0x48 (drawn this frame)
        uint8_t g49 = 0;       // AEGeometry +0x49 (base visibility)
        int32_t pos[3] = {};   // PlayerStaticFar +0xEC/+0xF0/+0xF4
    };

    volatile uintptr_t g_level = 0;
    uintptr_t g_gate = 0;          // slot 2 object captured after createSpace
    bool g_enabled = true;
    bool g_systemhasgate = true;
    int g_systemindex = -1;
    int g_stationindex = -1;
    int g_systemgatestation = -1;
    int g_rehidden = 0;
    int g_logged = 0;
    std::string g_last = "no level yet";

    std::string log_path()
    {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string p(path);
        size_t slash = p.find_last_of("\\/");
        return (slash == std::string::npos ? std::string() : p.substr(0, slash + 1)) + "gate_log.txt";
    }

    void log_line(const std::string& line)
    {
        FILE* f = std::fopen(log_path().c_str(), "a");
        if (!f)
            return;
        std::time_t now = std::time(nullptr);
        char stamp[32] = {};
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));
        std::fprintf(f, "[%s] %s\n", stamp, line.c_str());
        std::fclose(f);
    }

    bool read_slot(uintptr_t level, int index, Slot& s)
    {
        s = Slot{};
        uintptr_t landmarks = 0, array = 0;
        if (!rd(level + 0xD4, landmarks) || !rd(landmarks + 0x4, array) || !rd(array + index * 4, s.obj) || !s.obj)
            return false;
        rd(s.obj, s.vtable);
        rd(s.obj + 0xDD, s.visible);
        rd(s.obj + 0x8, s.geom);
        rd(s.obj + 0xEC, s.pos[0]);
        rd(s.obj + 0xF0, s.pos[1]);
        rd(s.obj + 0xF4, s.pos[2]);
        if (s.geom) {
            rd(s.geom + 0x48, s.g48);
            rd(s.geom + 0x49, s.g49);
        }
        return true;
    }

    // Same test as SolarSystem::currentOrbitHasWarpGate (0x4C39DE): system+0x2C == station index (+0x8).
    void read_system()
    {
        uintptr_t status = 0, station = 0, system = 0;
        g_systemindex = g_stationindex = g_systemgatestation = -1;
        g_systemhasgate = true; // unknown -> do nothing
        if (!rd(kStatus, status) || !status)
            return;
        rd(status + 0x160, station);
        rd(status + 0x168, system);
        if (station)
            rd(station + 0x8, g_stationindex);
        if (system) {
            rd(system + 0x2C, g_systemgatestation);
            rd(system + 0x8, g_systemindex);
            g_systemhasgate = g_systemgatestation >= 0;
        }
    }

    std::string describe(int index, const Slot& s)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "slot %d obj=%08X vt=%08X geom=%08X visible=%u g48=%u g49=%u pos=(%d, %d, %d)",
            index, (unsigned)s.obj, (unsigned)s.vtable, (unsigned)s.geom, s.visible, s.g48, s.g49, s.pos[0], s.pos[1], s.pos[2]);
        return buf;
    }

    // Is the captured slot 2 still the arrival gate of the current level? (the level is freed when docking)
    bool gate_still_valid(Slot& s)
    {
        uintptr_t level = g_level;
        if (!level || !g_gate)
            return false;
        if (!read_slot(level, kArrivalSlot, s))
            return false;
        return s.obj == g_gate && s.vtable == kJumpgateVtable && s.geom != 0;
    }

    void hide(const Slot& s)
    {
        wr<uint8_t>(s.obj + 0xDD, 0);
        wr<uint8_t>(s.geom + 0x48, 0);
        wr<uint8_t>(s.geom + 0x49, 0);
    }
}

void GateFix::after_createspace(uintptr_t level)
{
    g_level = 0;
    g_gate = 0;
    g_rehidden = 0;
    g_logged = 0;
    read_system();
    char head[200];
    std::snprintf(head, sizeof(head), "createSpace level=%08X system=%d station=%d system gate station=%d -> system has gate: %s",
        (unsigned)level, g_systemindex, g_stationindex, g_systemgatestation, g_systemhasgate ? "yes" : "no");
    log_line(head);
    for (int i = 0; i < 4; ++i) {
        Slot s;
        if (read_slot(level, i, s))
            log_line("  " + describe(i, s));
        else
            log_line("  slot " + std::to_string(i) + " empty");
    }
    Slot gate;
    if (read_slot(level, kArrivalSlot, gate) && gate.vtable == kJumpgateVtable && gate.geom) {
        g_gate = gate.obj;
        g_level = level;
        if (g_enabled && !g_systemhasgate && (gate.visible || gate.g48 || gate.g49)) {
            hide(gate);
            log_line("  slot 2 was visible right after createSpace -> hidden");
        }
    }
    g_last = head;
}

void GateFix::tick()
{
    Slot s;
    if (!gate_still_valid(s))
        return;
    if (s.visible || s.g48 || s.g49) {
        if (g_logged < 20) {
            ++g_logged;
            log_line("tick: " + describe(kArrivalSlot, s) + (g_enabled && !g_systemhasgate ? " -> hidden" : " (left as is)"));
        }
        if (g_enabled && !g_systemhasgate) {
            hide(s);
            ++g_rehidden;
        }
    }
}

void GateFix::draw_panel()
{
    ImGui::Separator();
    ImGui::TextUnformatted("Jumpgates (landmarks)");
    ImGui::Checkbox("Hide the arrival gate in systems without a jumpgate", &g_enabled);
    ImGui::TextWrapped("%s", g_last.c_str());
    ImGui::Text("re-hidden %d times since the level was created", g_rehidden);
    uintptr_t level = g_level;
    if (!level)
        return;
    for (int i = 0; i < 4; ++i) {
        Slot s;
        if (read_slot(level, i, s))
            ImGui::TextUnformatted(describe(i, s).c_str());
    }
}
