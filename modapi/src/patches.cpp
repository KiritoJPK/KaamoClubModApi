#include <windows.h>
#include <iostream>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <MinHook.h>
#include <filesystem>
#include <tlhelp32.h>
#include <vector>
#include <sol/sol.hpp>
#include <map>
#include <string>
#include "modapi_utils.h"
#include "luamanager.h"
#include "memoryutils.h"
#include "eventmanager.h"
#include "patches.h"
#include <Game/player.h>
#include <Game/system.h>
#include <Game/station.h>
#include <Game/mission.h>
#include <Game/asset.h>
#include <Game/structs.h>
#include "safety.h"

// "push imm" that holds the maximum number of star systems of the starmap
void Patches::patchstarmap(uint8_t new_limit)
{
    const uintptr_t addr = 0x004CE771;

    // push imm8 (0x6A) or push imm32 (0x68): in both cases the first operand byte is at addr + 1
    if (!Safety::expect_byte_in(addr, { 0x6A, 0x68 }, "starmap system limit"))
        return;
    Safety::write_bytes(addr + 1, &new_limit, 1, "starmap system limit");
}

// This patch is no longer used, it was useful to add a new station into stations.bin but now we are making stations in the runtime
// I didn't delete it just incase it's useful for anyone
void Patches::patchloadstations(uint8_t new_limit)
{
    uintptr_t addrs[] = { 0x004089B9, 0x00408DFE };

    for (size_t i = 0; i < 2; i++)
        Safety::write_bytes(addrs[i], &new_limit, 1, "stations limit"); // 0x6E
}

void Patches::patchmissions(uint8_t new_value)
{
    Safety::write_bytes(0x0049E212, &new_value, 1, "missions limit");
}

// The two conditional jumps of ModStation::onUpdate that decide if the valkyrie station is used.
// The original game has jnz (0x75) there, the enable patch turns them into jz (0x74).
static bool setvalkyriejumps(uint8_t opcode)
{
    const uintptr_t addrs[] = { 0x0049E1FD, 0x0049E205 };

    // check both before writing any so we never leave the two jumps in different states
    for (size_t i = 0; i < 2; i++) {
        if (!Safety::expect_byte_in(addrs[i], { 0x74, 0x75 }, "valkyrie conditional jump"))
            return false;
    }
    for (size_t i = 0; i < 2; i++)
        Safety::write_bytes(addrs[i], &opcode, 1, "valkyrie conditional jump");
    return true;
}

void Patches::patchvalkyrieenable()
{
    if (!setvalkyriejumps(0x74)) // jz
        return;
    MemoryUtils::Write<uint8_t>(Safety::exe_base() + 0x20AD4D, 1); // is valkyrie
    MemoryUtils::Write<uint8_t>(Safety::exe_base() + 0x20AD4F, 1); // is valkyrie
}

void Patches::patchvalkyriedisable()
{
    if (!setvalkyriejumps(0x75)) // jnz
        return;
    MemoryUtils::Write<uint8_t>(Safety::exe_base() + 0x20AD4D, 0); // is valkyrie
    MemoryUtils::Write<uint8_t>(Safety::exe_base() + 0x20AD4F, 0); // is valkyrie
}
