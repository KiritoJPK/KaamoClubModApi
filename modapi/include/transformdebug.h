#ifndef TRANSFORMDEBUG_H
#define TRANSFORMDEBUG_H

// Diagnostic window (F8) that lists the transforms of the engine (PaintCanvas) and writes
// transform_dump.txt next to GOF2.exe. Used to find why some station parts are drawn at the
// position of the planet (Kothar, Valkyrie). Temporary, read only.
namespace TransformDebug {
    void register_window();
}
#endif
