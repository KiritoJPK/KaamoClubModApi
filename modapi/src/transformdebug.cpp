#include "transformdebug.h"
#include "imguihandler.h"
#include "offset.h"
#include <imgui.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    // PaintCanvas (Offset::GLOBALS_PAINTCANVAS): +0x168 number of transforms, +0x16C Transform* array
    // (see PaintCanvas::TransformAddChild at 0x4EA9F0). Transform fields, same layout as the Android build:
    // +0x3C mesh count, +0x40 Mesh* array, +0x4C child count, +0x50 Transform* children,
    // +0x58 animation mode, +0xD4 bounding sphere (x, y, z, radius), +0xED animation running
    constexpr uintptr_t kCanvas = 0x60AE68;
    constexpr size_t kRawSize = 0x100;

    template <typename T>
    bool rd(uintptr_t address, T& out)
    {
        SIZE_T done = 0;
        return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &out, sizeof(T), &done) && done == sizeof(T);
    }

    bool rdbuf(uintptr_t address, void* out, size_t size)
    {
        SIZE_T done = 0;
        return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &done) && done == size;
    }

    struct Info {
        uint32_t index = 0;
        uintptr_t ptr = 0;
        int32_t meshes = 0;
        int32_t children = 0;
        int32_t mode = 0;
        uint8_t anim = 0;
        float sphere[4] = {};
        std::vector<uintptr_t> childptrs;
        std::vector<uintptr_t> meshptrs;
    };

    std::vector<Info> g_infos;
    std::string g_status = "press Dump";

    bool collect(std::vector<Info>& infos)
    {
        infos.clear();
        uintptr_t canvas = 0;
        if (!rd(kCanvas, canvas) || !canvas)
            return false;
        uint32_t count = 0;
        uintptr_t array = 0;
        if (!rd(canvas + 0x168, count) || !rd(canvas + 0x16C, array) || !array || count > 200000)
            return false;
        for (uint32_t i = 0; i < count; ++i) {
            Info info;
            info.index = i;
            if (!rd(array + i * 4, info.ptr) || !info.ptr)
                continue;
            uintptr_t t = info.ptr;
            rd(t + 0x3C, info.meshes);
            rd(t + 0x4C, info.children);
            rd(t + 0x58, info.mode);
            rd(t + 0xED, info.anim);
            rdbuf(t + 0xD4, info.sphere, sizeof(info.sphere));
            uintptr_t list = 0;
            if (info.children > 0 && info.children < 64 && rd(t + 0x50, list)) {
                for (int c = 0; c < info.children; ++c) {
                    uintptr_t child = 0;
                    if (rd(list + c * 4, child))
                        info.childptrs.push_back(child);
                }
            }
            if (info.meshes > 0 && info.meshes < 64 && rd(t + 0x40, list)) {
                for (int m = 0; m < info.meshes; ++m) {
                    uintptr_t mesh = 0;
                    if (rd(list + m * 4, mesh))
                        info.meshptrs.push_back(mesh);
                }
            }
            infos.push_back(std::move(info));
        }
        return true;
    }

    void write_raw(FILE* f, const char* label, uintptr_t address, size_t size)
    {
        std::vector<uint8_t> raw(size);
        if (!rdbuf(address, raw.data(), size)) {
            std::fprintf(f, "    %s %08X unreadable\n", label, static_cast<unsigned>(address));
            return;
        }
        for (size_t o = 0; o < size; o += 16) {
            std::fprintf(f, "    %s +%03X:", label, static_cast<unsigned>(o));
            for (size_t k = 0; k < 16; k += 4) {
                uint32_t v;
                std::memcpy(&v, raw.data() + o + k, 4);
                float fv;
                std::memcpy(&fv, &v, 4);
                std::fprintf(f, " %08X(%g)", v, fv);
            }
            std::fprintf(f, "\n");
        }
    }

    void dump()
    {
        std::vector<Info> infos;
        if (!collect(infos)) {
            g_status = "PaintCanvas not available";
            return;
        }
        std::unordered_map<uintptr_t, uint32_t> byptr;
        std::unordered_map<uintptr_t, uint32_t> parentof;
        for (const Info& info : infos) {
            byptr[info.ptr] = info.index;
            for (uintptr_t c : info.childptrs)
                parentof[c] = info.index;
        }
        FILE* f = nullptr;
        if (fopen_s(&f, "transform_dump.txt", "w") != 0 || !f) {
            g_status = "could not write transform_dump.txt";
            return;
        }
        std::fprintf(f, "transforms: %u\n", static_cast<unsigned>(infos.size()));
        std::fprintf(f, "index ptr meshes children anim mode sphere(x y z r) parent\n");
        for (const Info& info : infos) {
            auto p = parentof.find(info.ptr);
            std::fprintf(f, "%u %08X m=%d c=%d anim=%u mode=%d s=(%g %g %g %g) parent=%d\n", info.index, static_cast<unsigned>(info.ptr),
                info.meshes, info.children, info.anim, info.mode, info.sphere[0], info.sphere[1], info.sphere[2], info.sphere[3],
                p == parentof.end() ? -1 : static_cast<int>(p->second));
        }
        std::fprintf(f, "\n=== parents and children (raw) ===\n");
        for (const Info& info : infos) {
            bool ischild = parentof.count(info.ptr) != 0;
            if (info.childptrs.empty() && !ischild)
                continue;
            std::fprintf(f, "\n[%u] %08X children:", info.index, static_cast<unsigned>(info.ptr));
            for (uintptr_t c : info.childptrs) {
                auto it = byptr.find(c);
                std::fprintf(f, " %d", it == byptr.end() ? -1 : static_cast<int>(it->second));
            }
            std::fprintf(f, "\n");
            write_raw(f, "T", info.ptr, kRawSize);
            for (size_t m = 0; m < info.meshptrs.size(); ++m) {
                char label[16];
                std::snprintf(label, sizeof(label), "M%u", static_cast<unsigned>(m));
                write_raw(f, label, info.meshptrs[m], 0x60);
            }
        }
        std::fclose(f);
        g_infos = std::move(infos);
        g_status = "transform_dump.txt written (" + std::to_string(g_infos.size()) + " transforms)";
    }

    void draw()
    {
        if (ImGui::Button("Dump transforms"))
            dump();
        ImGui::SameLine();
        ImGui::TextUnformatted(g_status.c_str());
        ImGui::Separator();
        for (const Info& info : g_infos) {
            if (info.childptrs.empty())
                continue;
            ImGui::Text("[%u] m=%d c=%d anim=%u sphere=(%.0f %.0f %.0f r=%.0f)", info.index, info.meshes, info.children, info.anim,
                info.sphere[0], info.sphere[1], info.sphere[2], info.sphere[3]);
        }
    }
}

void TransformDebug::register_window()
{
    ImGuiHandler::add_window("GOF2 transform debug (F8)", draw, VK_F8, false);
    ImGuiHandler::set_windowopen("GOF2 transform debug (F8)", false);
}
