#ifndef LANGCOMPAT_H
#define LANGCOMPAT_H
#include <cstdint>

struct AEString;

// The PC release of GOF2 ships .lang files without the 10 texts that only exist on the other
// platforms (Supernova Challenge + 2 Supernova texts), so every text id after them is shifted.
// The game and every mod use the PC ids. When the loaded .lang file has the layout of the other
// platforms (macOS / iOS / Android), the ids are translated here before the text is read,
// so both layouts work with the same exe.
namespace LangCompat {
    enum class Layout { Unknown, PC, Mobile };

    constexpr int32_t kPCTextCount = 3361;
    constexpr int32_t kMobileTextCount = 3371;

    // layout of the .lang file currently loaded by the game
    Layout layout();
    // PC text id -> id inside the loaded .lang file
    int32_t to_loaded_id(int32_t pcid);
    // reads the text like GameText::getText does, using the translated id.
    // returns nullptr when the game hasn't loaded texts yet or the id is out of range
    AEString* get_text(int32_t pcid);
}
#endif
