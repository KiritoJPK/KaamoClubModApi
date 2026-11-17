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

    // GameText::loadLanguage (0x4F7950) has the number of texts hard coded (3361). The hook counts
    // the texts of the file that is going to be loaded and sets that number before the game reads it,
    // otherwise the game drops the last texts of bigger files and frees every text of smaller ones.
    using loadlanguage_t = void(__stdcall*)(void* gametext, int32_t language);
    inline loadlanguage_t original_loadlanguage = nullptr;
    void __stdcall loadlanguage_hook(void* gametext, int32_t language);

    // index used by the game: 0 gb, 1 de, 2 fr, 3 it, 4 es, 5 ru, 6 pl, 7 pt, 8 cz, 9 ar,
    // 10 zs, 11 zt, 12 ptl (Portugues - Brasil), 13 mex, 14 ko, 15 ja
    constexpr int32_t kLanguageCount = 16;
    const char* language_file(int32_t language);
    int32_t current_language();
    // Loads another language like the options menu does. Call it from the game thread (Lua events).
    bool set_language(int32_t language);

    // Adds "Português (Brasil)" (ptl.lang, index 12) to the language list of the menu.
    // The exe of the PC knows this language but its menu only has 11 entries.
    void install_language_menu_entry();
}
#endif
