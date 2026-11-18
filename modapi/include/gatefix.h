#ifndef GATEFIX_H
#define GATEFIX_H
#include <cstdint>

// Ghost jumpgate in systems without a gate (Beidan/Kothar, Valkyrie).
// Level::createSpace (0x467D5A) creates the landmarks of the level: slot 0 station, slot 1 the system
// jumpgate (only when the system has one), slot 2 the arrival gate of the jump drive (created hidden,
// the Android build never shows it), slot 3 the wormhole. On PC the slot 2 gate ends up drawn, so a
// terran jumpgate appears in systems that have none. The fix keeps slot 2 hidden when the current
// system has no jumpgate, and logs the landmarks to gate_log.txt (next to GOF2.exe).
namespace GateFix {
    void after_createspace(uintptr_t level);
    void tick();          // called once per frame (EndScene)
    void draw_panel();    // ImGui section inside the debug window
}
#endif
