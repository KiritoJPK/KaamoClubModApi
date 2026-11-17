#ifndef SAFETY_H
#define SAFETY_H
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

// Runtime safety net of the api.
//  - fingerprints GOF2.exe and checks that it is mapped where every hard coded offset expects it
//  - validates the address of every hook / patch and checks the result of every hook / patch
//  - keeps a registry of the installed hooks (the crash handler uses it to name game addresses)
//  - safe mode: when the api can't be installed reliably it disables itself and the game runs vanilla
namespace Safety {
    // every absolute address of offset.h / patches.cpp / hooks.cpp assumes this image base
    constexpr uintptr_t kExpectedImageBase = 0x400000;

    struct BuildInfo {
        bool valid = false;           // PE headers were parsed
        bool basematches = false;     // mapped at kExpectedImageBase
        bool known = false;           // fingerprint is in the known builds table (safety.cpp)
        std::string knowndescription;
        uintptr_t imagebase = 0;      // where the exe really is in memory
        uint32_t preferredbase = 0;   // ImageBase of the PE header
        uint32_t timestamp = 0;
        uint32_t sizeofimage = 0;
        uint32_t checksum = 0;
        uint32_t entrypointrva = 0;
        uint32_t codecrc32 = 0;       // crc32 of the executable sections
    };

    struct HookRecord {
        std::string name;
        uintptr_t target = 0;
        bool installed = false;
        std::string error;
    };

    // Reads the PE headers of the running exe, prints its fingerprint and checks the image base.
    // Returns false (and enters safe mode) when the exe is not usable.
    bool inspect_game_build();
    // Same as above for any mapped image (used by inspect_game_build, handy for tests)
    bool inspect_image(uintptr_t imagebase);
    const BuildInfo& build();
    std::string build_summary();
    uintptr_t exe_base();

    bool is_image_address(uintptr_t addr, size_t size = 1);
    bool is_code_address(uintptr_t addr);
    // " [GOF2.exe+0x7B2F1 | near LEVEL_UPDATE+0x31]" for game addresses, empty string otherwise
    std::string describe_address(uintptr_t addr);

    // MH_CreateHook for an address inside GOF2.exe. Checks the address and the MinHook status,
    // logs failures and records the hook. 'expr' is the text of the target expression (for the log).
    bool create_game_hook(const char* expr, void* target, void* detour, void** original);
    const std::vector<HookRecord>& hooks();
    size_t failed_hook_count();
    // false if an essential hook is missing or too many hooks failed (= almost certainly a different exe)
    bool hooks_acceptable(std::string* reason);

    // Verified memory patching. Everything is logged on failure.
    bool write_bytes(uintptr_t addr, const void* data, size_t size, const char* what);
    // Writes a 32 bit pointer operand. If expected_old is not 0 and the current value differs a
    // warning is logged (the value is still written).
    bool write_pointer(uintptr_t site, const void* value, const char* what, uintptr_t expected_old = 0);
    // true if the byte at addr is one of 'allowed'. Logs and returns false otherwise.
    bool expect_byte_in(uintptr_t addr, std::initializer_list<uint8_t> allowed, const char* what);

    // Safe mode: the api stays loaded but installs nothing and runs no mod.
    void enter_safe_mode(const std::string& reason);
    bool safe_mode();
    const std::string& safe_mode_reason();
    void show_safe_mode_notice();
}
#endif
