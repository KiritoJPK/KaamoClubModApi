#include "datapaths.h"
#include "safety.h"
#include <windows.h>
#include <cstring>
#include <iostream>

namespace {
    struct PathPatch {
        uintptr_t address;       // string in .rdata of GOF2.exe
        const char* original;
        const char* relocated;
        const char* check;       // file or folder that must exist to use the new path
        const char* what;
    };

    const PathPatch kLangPath = { 0x533278, "data\\assets\\main\\lang\\", "data\\lang\\", "data\\lang\\gb.lang", "lang folder" };
    const PathPatch kSoundPath = { 0x54E178, "data\\assets\\main\\sound\\FMOD_GOF2.fev", "data\\sound\\FMOD_GOF2.fev", "data\\sound\\FMOD_GOF2.fev", "sound folder" };

    const char* g_langfolder = "data\\assets\\main\\lang\\";

    bool exists(const char* path)
    {
        return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }

    bool relocate(const PathPatch& patch)
    {
        if (!exists(patch.check))
            return false;
        const char* current = reinterpret_cast<const char*>(patch.address);
        if (std::strcmp(current, patch.relocated) == 0)
            return true;
        if (std::strcmp(current, patch.original) != 0) {
            std::cout << "[DataPaths] " << patch.what << ": the exe doesn't have the expected path, not changed" << std::endl;
            return false;
        }
        // the new path is shorter, it is written over the old one (with its terminating zero)
        if (!Safety::write_bytes(patch.address, patch.relocated, std::strlen(patch.relocated) + 1, patch.what))
            return false;
        std::cout << "[DataPaths] " << patch.what << " -> " << patch.relocated << std::endl;
        return true;
    }
}

void DataPaths::apply()
{
    if (relocate(kLangPath))
        g_langfolder = kLangPath.relocated;
    relocate(kSoundPath);
}

const char* DataPaths::lang_folder()
{
    return g_langfolder;
}
