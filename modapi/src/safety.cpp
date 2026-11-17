#include "safety.h"
#include <MinHook.h>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
    struct SectionRange {
        uintptr_t start;
        uintptr_t end;
        bool executable;
    };

    struct KnownBuild {
        uint32_t timestamp;
        uint32_t sizeofimage;
        uint32_t codecrc32;
        const char* description;
    };

    // Add one line per GOF2.exe the api was tested against, using the fingerprint printed in the
    // console at startup ("GOF2.exe fingerprint: ..."). Builds missing from this table still work,
    // the table only lets the api (and the crash logs) say if the exe is a known one.
    const KnownBuild kKnownBuilds[] = {
        { 0x502B585F, 0x0025D000, 0xCAAB6E87, "GOF2.exe PC, build 2012-08-15 (image base 0x400000, no ASLR)" },
        { 0, 0, 0, nullptr }, // sentinel, keep last
    };

    // Without these hooks no mod can work at all
    const char* const kEssentialHooks[] = { "GLOBALS_INIT_ADDR" };
    // More than this percentage of failed hooks means the exe is not the one the offsets were made for
    constexpr size_t kMaxFailedHookPercent = 25;
    // A hook is named after the closest installed hook below the address, only if it is this close
    constexpr uintptr_t kMaxNearHookDistance = 0x1000;

    Safety::BuildInfo g_build;
    std::vector<SectionRange> g_sections;
    std::vector<Safety::HookRecord> g_hooks;
    bool g_safemode = false;
    std::string g_safemode_reason;

    std::string hex(uintptr_t value, int width = 0)
    {
        std::ostringstream out;
        out << "0x" << std::uppercase << std::hex;
        if (width > 0)
            out << std::setw(width) << std::setfill('0');
        out << value;
        return out.str();
    }

    uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t length)
    {
        static uint32_t table[256];
        static bool ready = false;

        if (!ready) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            ready = true;
        }
        for (size_t i = 0; i < length; ++i)
            crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        return crc;
    }

    // "(LPVOID)Offset::LEVEL_UPDATE" -> "LEVEL_UPDATE"
    std::string clean_name(const char* expr)
    {
        std::string name = expr ? expr : "";
        size_t pos = name.rfind("::");

        if (pos != std::string::npos)
            name = name.substr(pos + 2);
        while (!name.empty() && (name.back() == ')' || name.back() == ' '))
            name.pop_back();
        return name.empty() ? std::string("<unnamed>") : name;
    }

    bool is_essential(const std::string& name)
    {
        for (const char* essential : kEssentialHooks) {
            if (name == essential)
                return true;
        }
        return false;
    }

    void log_failure(const std::string& message)
    {
        std::cout << "[-] " << message << std::endl;
    }
}

bool Safety::inspect_image(uintptr_t imagebase)
{
    g_build = BuildInfo();
    g_sections.clear();
    g_build.imagebase = imagebase;
    if (imagebase == 0)
        return false;

    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imagebase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) || dos->e_lfanew > 0x1000)
        return false;
    const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(imagebase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;

    g_build.preferredbase = nt->OptionalHeader.ImageBase;
    g_build.timestamp = nt->FileHeader.TimeDateStamp;
    g_build.sizeofimage = nt->OptionalHeader.SizeOfImage;
    g_build.checksum = nt->OptionalHeader.CheckSum;
    g_build.entrypointrva = nt->OptionalHeader.AddressOfEntryPoint;

    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    uint32_t crc = 0xFFFFFFFFu;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER& sec = sections[i];
        uint32_t virtualsize = sec.Misc.VirtualSize;
        uint32_t rawsize = sec.SizeOfRawData;
        uint32_t extent = virtualsize > rawsize ? virtualsize : rawsize;
        SectionRange range;

        range.start = imagebase + sec.VirtualAddress;
        range.end = range.start + extent;
        range.executable = (sec.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        g_sections.push_back(range);
        if (range.executable) {
            // only the part of the section that comes from the file (the rest is zero filled)
            uint32_t hashsize = (virtualsize != 0 && virtualsize < rawsize) ? virtualsize : rawsize;
            // never read past the mapped image, even if the headers are inconsistent
            if (sec.VirtualAddress < g_build.sizeofimage && hashsize <= g_build.sizeofimage - sec.VirtualAddress)
                crc = crc32_update(crc, reinterpret_cast<const uint8_t*>(range.start), hashsize);
        }
    }
    g_build.codecrc32 = ~crc;
    g_build.valid = true;
    g_build.basematches = (imagebase == kExpectedImageBase);
    for (const KnownBuild& known : kKnownBuilds) {
        if (!known.description)
            break;
        if (known.timestamp == g_build.timestamp && known.sizeofimage == g_build.sizeofimage && known.codecrc32 == g_build.codecrc32) {
            g_build.known = true;
            g_build.knowndescription = known.description;
            break;
        }
    }
    return true;
}

bool Safety::inspect_game_build()
{
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));

    if (!inspect_image(base)) {
        enter_safe_mode("the PE headers of the game executable could not be read");
        return false;
    }
    std::cout << "[+] " << build_summary() << std::endl;
    if (!g_build.basematches) {
        enter_safe_mode("GOF2.exe is loaded at " + hex(g_build.imagebase, 8) + " but every offset of the api assumes " + hex(kExpectedImageBase, 8) + " (relocated image / ASLR)");
        return false;
    }
    return true;
}

const Safety::BuildInfo& Safety::build()
{
    return g_build;
}

std::string Safety::build_summary()
{
    std::ostringstream out;

    out << "GOF2.exe fingerprint: base=" << hex(g_build.imagebase, 8)
        << " timestamp=" << hex(g_build.timestamp, 8)
        << " sizeofimage=" << hex(g_build.sizeofimage)
        << " checksum=" << hex(g_build.checksum)
        << " entry=" << hex(g_build.entrypointrva)
        << " codecrc32=" << hex(g_build.codecrc32, 8)
        << (g_build.known ? " (known build: " + g_build.knowndescription + ")" : " (build not in the known builds table)");
    return out.str();
}

uintptr_t Safety::exe_base()
{
    return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
}

bool Safety::is_image_address(uintptr_t addr, size_t size)
{
    if (!g_build.valid || size == 0)
        return false;
    uintptr_t end = addr + size;
    return end > addr && addr >= g_build.imagebase && end <= g_build.imagebase + g_build.sizeofimage;
}

bool Safety::is_code_address(uintptr_t addr)
{
    for (const SectionRange& range : g_sections) {
        if (range.executable && addr >= range.start && addr < range.end)
            return true;
    }
    return false;
}

std::string Safety::describe_address(uintptr_t addr)
{
    if (!is_image_address(addr))
        return std::string();

    std::ostringstream out;
    const HookRecord* nearest = nullptr;

    out << " [GOF2.exe+" << hex(addr - g_build.imagebase);
    for (const HookRecord& hook : g_hooks) {
        if (hook.target <= addr && addr - hook.target <= kMaxNearHookDistance) {
            if (!nearest || hook.target > nearest->target)
                nearest = &hook;
        }
    }
    if (nearest)
        out << " | near " << nearest->name << "+" << hex(addr - nearest->target);
    out << "]";
    return out.str();
}

bool Safety::create_game_hook(const char* expr, void* target, void* detour, void** original)
{
    HookRecord record;
    std::string error;

    record.name = clean_name(expr);
    record.target = reinterpret_cast<uintptr_t>(target);
    if (!g_build.valid || !g_build.basematches)
        error = "the game executable was not validated";
    else if (!is_code_address(record.target))
        error = "the address is not inside an executable section of GOF2.exe";
    else {
        MH_STATUS status = MH_CreateHook(target, detour, original);
        if (status != MH_OK)
            error = std::string("MinHook: ") + MH_StatusToString(status);
    }
    record.installed = error.empty();
    record.error = error;
    g_hooks.push_back(record);
    if (!record.installed)
        log_failure("hook " + record.name + " (" + hex(record.target, 8) + ") was not installed: " + error);
    return record.installed;
}

const std::vector<Safety::HookRecord>& Safety::hooks()
{
    return g_hooks;
}

size_t Safety::failed_hook_count()
{
    size_t failed = 0;

    for (const HookRecord& hook : g_hooks) {
        if (!hook.installed)
            ++failed;
    }
    return failed;
}

bool Safety::hooks_acceptable(std::string* reason)
{
    size_t failed = failed_hook_count();

    for (const HookRecord& hook : g_hooks) {
        if (!hook.installed && is_essential(hook.name)) {
            if (reason)
                *reason = "the essential hook " + hook.name + " could not be installed: " + hook.error;
            return false;
        }
    }
    if (failed * 100 > g_hooks.size() * kMaxFailedHookPercent) {
        if (reason)
            *reason = std::to_string(failed) + " of " + std::to_string(g_hooks.size()) + " hooks could not be installed, this GOF2.exe is probably not the version the api was made for";
        return false;
    }
    return true;
}

bool Safety::write_bytes(uintptr_t addr, const void* data, size_t size, const char* what)
{
    DWORD oldprotect = 0;
    DWORD ignored = 0;
    std::string label = what ? what : "patch";

    if (!data || size == 0)
        return false;
    if (!is_image_address(addr, size)) {
        log_failure("patch '" + label + "' refused: " + hex(addr, 8) + " is outside GOF2.exe");
        return false;
    }
    if (!VirtualProtect(reinterpret_cast<LPVOID>(addr), size, PAGE_EXECUTE_READWRITE, &oldprotect)) {
        log_failure("patch '" + label + "' failed: VirtualProtect error " + std::to_string(GetLastError()) + " at " + hex(addr, 8));
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(addr), data, size);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), size);
    VirtualProtect(reinterpret_cast<LPVOID>(addr), size, oldprotect, &ignored);
    return true;
}

bool Safety::write_pointer(uintptr_t site, const void* value, const char* what, uintptr_t expected_old)
{
    std::string label = what ? what : "pointer patch";
    uint32_t newvalue = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));

    if (!is_image_address(site, sizeof(uint32_t))) {
        log_failure("patch '" + label + "' refused: " + hex(site, 8) + " is outside GOF2.exe");
        return false;
    }
    if (expected_old != 0) {
        uint32_t oldvalue = 0;
        std::memcpy(&oldvalue, reinterpret_cast<const void*>(site), sizeof(oldvalue));
        if (oldvalue != static_cast<uint32_t>(expected_old))
            std::cout << "[!] patch '" << label << "' at " << hex(site, 8) << ": the original operand is " << hex(oldvalue, 8) << ", expected " << hex(expected_old, 8) << " (different GOF2.exe?)" << std::endl;
    }
    return write_bytes(site, &newvalue, sizeof(newvalue), what);
}

bool Safety::expect_byte_in(uintptr_t addr, std::initializer_list<uint8_t> allowed, const char* what)
{
    std::string label = what ? what : "patch";

    if (!is_image_address(addr)) {
        log_failure("patch '" + label + "' refused: " + hex(addr, 8) + " is outside GOF2.exe");
        return false;
    }
    uint8_t current = *reinterpret_cast<const uint8_t*>(addr);
    for (uint8_t value : allowed) {
        if (current == value)
            return true;
    }
    log_failure("patch '" + label + "' refused: unexpected byte " + hex(current, 2) + " at " + hex(addr, 8) + " (different GOF2.exe?)");
    return false;
}

void Safety::enter_safe_mode(const std::string& reason)
{
    if (g_safemode)
        return;
    g_safemode = true;
    g_safemode_reason = reason;
    std::cout << "[-] SAFE MODE: " << reason << std::endl;
}

bool Safety::safe_mode()
{
    return g_safemode;
}

const std::string& Safety::safe_mode_reason()
{
    return g_safemode_reason;
}

void Safety::show_safe_mode_notice()
{
    std::string text = "KaamoClubModApi disabled itself for this session, the game will run without mods.\n\nReason: " + g_safemode_reason + "\n\n" + build_summary() +
        "\n\nThe same details are in the console window. Please include them when you report this problem.";

    std::cout << "[-] KaamoClubModApi is disabled, the game runs without mods." << std::endl;
    MessageBoxA(nullptr, text.c_str(), "KaamoClubModApi", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}
