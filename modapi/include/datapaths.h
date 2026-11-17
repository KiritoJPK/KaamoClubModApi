#ifndef DATAPATHS_H
#define DATAPATHS_H

// The game reads its texts from "data\assets\main\lang" and its sounds from "data\assets\main\sound"
// (FMOD loads the .fsb banks from the folder of FMOD_GOF2.fev). These folders can also be placed
// directly in "data" (as "data\lang" and "data\sound"): when they exist there, the paths inside the exe
// are changed before the game uses them. The original layout keeps working.
namespace DataPaths {
    // must run before the game initializes its globals (Hooks::init does it first)
    void apply();
    // folder of the .lang files the game is using, with the trailing backslash
    const char* lang_folder();
}
#endif
