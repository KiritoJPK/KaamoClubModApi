#include "langcompat.h"
#include "offset.h"
#include <Game/structs.h>
#include "safety.h"
#include "datapaths.h"
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
    // GameText object, see GameText::getText (0x4F38B0)
    struct GameTextView {
        int32_t pairsint;      // +0x00 number of ints of the id replacement table (2 ints per entry)
        int32_t* pairs;        // +0x04 { from, to } id replacements
        uint8_t pad08[4];
        AEString** texts;      // +0x0C
        uint8_t pad10[0x10];
        int32_t textcount;     // +0x20
    };
    static_assert(offsetof(GameTextView, texts) == 0x0C, "GameText layout");
    static_assert(offsetof(GameTextView, textcount) == 0x20, "GameText layout");

    // texts the PC .lang files don't have, as positions in PC ids: every text with
    // a PC id >= first is moved by count in the mobile layout
    struct Insertion {
        int32_t first;
        int32_t count;
    };
    // mobile 282..289: Supernova Challenge texts
    // mobile 3208: "The specter is now available on Katashán station in the Paréah system."
    // mobile 3211: "Gas cloud"
    constexpr Insertion kMobileInsertions[] = { { 282, 8 }, { 3200, 1 }, { 3202, 1 } };

    GameTextView* gametext()
    {
        return *reinterpret_cast<GameTextView**>(Offset::GLOBALS_GAMETEXT);
    }

    LangCompat::Layout g_lastlayout = LangCompat::Layout::Unknown;
    int32_t g_lastcount = -1;
}

LangCompat::Layout LangCompat::layout()
{
    GameTextView* gt = gametext();
    if (!gt || !gt->texts)
        return Layout::Unknown;
    Layout result = Layout::Unknown;
    if (gt->textcount == kMobileTextCount)
        result = Layout::Mobile;
    else if (gt->textcount == kPCTextCount)
        result = Layout::PC;
    if (gt->textcount != g_lastcount) {
        // logged once per loaded language file
        g_lastcount = gt->textcount;
        g_lastlayout = result;
        std::cout << "[LangCompat] loaded .lang has " << gt->textcount << " texts: "
            << (result == Layout::Mobile ? "macOS/mobile layout, PC text ids are translated"
                : result == Layout::PC ? "PC layout"
                : "unknown layout, text ids are used as they are") << std::endl;
    }
    return result;
}

int32_t LangCompat::to_loaded_id(int32_t pcid)
{
    if (pcid < 0 || layout() != Layout::Mobile)
        return pcid;
    int32_t shift = 0;
    for (const Insertion& ins : kMobileInsertions) {
        if (pcid >= ins.first)
            shift += ins.count;
    }
    return pcid + shift;
}

AEString* LangCompat::get_text(int32_t pcid)
{
    GameTextView* gt = gametext();
    if (!gt || !gt->texts)
        return nullptr;
    // same order as the game: first the replacement table (it holds PC ids), then the lookup
    int32_t id = pcid;
    for (int32_t i = 0; i + 1 < gt->pairsint; i += 2) {
        if (gt->pairs[i] == id) {
            id = gt->pairs[i + 1];
            break;
        }
    }
    id = to_loaded_id(id);
    if (id < 0 || id >= gt->textcount)
        return nullptr;
    return gt->texts[id];
}

namespace {
    // the three places of GameText::loadLanguage that hold the number of texts
    constexpr uintptr_t kCountSite = 0x4F7998;       // mov eax, 3361 (size of the text array)
    constexpr uintptr_t kClearLoopSite = 0x4F79C9;   // cmp eax, 3361 * 4 (array clear loop)
    constexpr uintptr_t kReadLoopSite = 0x4F834B;    // cmp eax, 3361 * 4 (read loop, in GameText::read)
    constexpr int32_t kMaxTextCount = 20000;
    int32_t g_capacity = LangCompat::kPCTextCount;
    bool g_capacitybroken = false;

    const char* const kLanguageFiles[LangCompat::kLanguageCount] = {
        "gb.lang", "de.lang", "fr.lang", "it.lang", "es.lang", "ru.lang", "pl.lang", "pt.lang",
        "cz.lang", "ar.lang", "zs.lang", "zt.lang", "ptl.lang", "mex.lang", "ko.lang", "ja.lang"
    };

    // number of texts of a .lang file (uint16 big endian byte length + utf8 text, repeated), -1 if invalid
    int32_t count_texts(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return -1;
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        size_t pos = 0;
        int32_t count = 0;
        while (pos + 2 <= data.size()) {
            size_t length = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
            pos += 2 + length;
            ++count;
        }
        return pos == data.size() && count > 0 ? count : -1;
    }

    bool read_imm32(uintptr_t site, uint8_t opcode, int32_t& value)
    {
        const uint8_t* code = reinterpret_cast<const uint8_t*>(site);
        if (code[0] != opcode)
            return false;
        value = *reinterpret_cast<const int32_t*>(site + 1);
        return true;
    }

    bool set_capacity(int32_t count)
    {
        if (g_capacitybroken)
            return false;
        if (count == g_capacity)
            return true;
        int32_t size = 0, clearloop = 0, readloop = 0;
        // check the code is still the one this was written for before touching it
        if (!read_imm32(kCountSite, 0xB8, size) || !read_imm32(kClearLoopSite, 0x3D, clearloop) || !read_imm32(kReadLoopSite, 0x3D, readloop)
            || size != g_capacity || clearloop != g_capacity * 4 || readloop != g_capacity * 4) {
            std::cout << "[LangCompat] GameText::loadLanguage doesn't look as expected, text count not changed" << std::endl;
            g_capacitybroken = true;
            return false;
        }
        int32_t bytes = count * 4;
        if (!Safety::write_bytes(kCountSite + 1, &count, 4, "text count")
            || !Safety::write_bytes(kClearLoopSite + 1, &bytes, 4, "text count (clear loop)")
            || !Safety::write_bytes(kReadLoopSite + 1, &bytes, 4, "text count (read loop)")) {
            g_capacitybroken = true;
            return false;
        }
        g_capacity = count;
        return true;
    }
}

const char* LangCompat::language_file(int32_t language)
{
    // the game loads gb.lang for every unknown index
    if (language < 0 || language >= kLanguageCount)
        return kLanguageFiles[0];
    return kLanguageFiles[language];
}

int32_t LangCompat::current_language()
{
    return *reinterpret_cast<int16_t*>(Offset::GLOBALS_LANGUAGE);
}

void __stdcall LangCompat::loadlanguage_hook(void* gametext, int32_t language)
{
    const std::string folder = DataPaths::lang_folder();
    int16_t index = static_cast<int16_t>(language);
    int32_t count = count_texts(folder + language_file(index));
    // the game falls back to gb.lang when the file can't be loaded
    if (count < 0)
        count = count_texts(folder + language_file(0));
    if (count > 0 && count <= kMaxTextCount && set_capacity(count))
        std::cout << "[LangCompat] loading " << language_file(index) << " with " << count << " texts" << std::endl;
    original_loadlanguage(gametext, language);
}

bool LangCompat::set_language(int32_t language)
{
    if (language < 0 || language >= kLanguageCount)
        return false;
    void* gametextptr = *reinterpret_cast<void**>(Offset::GLOBALS_GAMETEXT);
    if (!gametextptr)
        return false;
    uintptr_t setfont = Offset::GLOBALS_SETLANGUAGEFONT;
    uintptr_t load = Offset::GAMETEXT_LOADLANGUAGE;
    // same calls as the language menu of the options
    __asm {
        push ebx
        push esi
        push edi
        mov eax, language
        call setfont
        push language
        push gametextptr
        call load
        pop edi
        pop esi
        pop ebx
    }
    return current_language() == language;
}

// ---- "Português (Brasil)" in the language menu ----
//
// The menu (built at 0x4845F0, MenuTouchWindow) creates its buttons from two arrays on the stack:
// button ids (0x1B + position in the jump table of the click handler) and text ids of the names.
// The loop runs 11 times. Two reads of those arrays are replaced by calls to the stubs below,
// that insert a 12th entry after Русский, and the loop runs 12 times. The click handler (0x48ED40)
// only knows the button ids 0x1B..0x26, its range check jumps to a stub that adds 0x27.
namespace {

    constexpr uintptr_t kButtonIdRead = 0x4848EA;  // mov eax, [esp+eax+0x98]
    constexpr uintptr_t kNameTextRead = 0x484985;  // mov eax, [esp+eax+0xC4]
    constexpr uintptr_t kLoopCount = 0x484A39;     // cmp dword [esp+0x24], 0Bh (immediate)
    constexpr uintptr_t kHandlerCheck = 0x48ED46;  // cmp eax, esi / ja 0x48EDE5 / jmp [eax*4+0x48EF62]

    __declspec(naked) void buttonid_stub()
    {
        __asm {
            // eax = position * 4. Português goes to position 7 (after Русский),
            // the entries from there on move one position down
            cmp eax, 0x1C
            jb normal
            je extra
            sub eax, 4
        normal:
            mov eax, [esp + eax + 0x98 + 4] // + 4: return address of the call
            ret
        extra:
            mov eax, 0x27
            ret
        }
    }

    __declspec(naked) void nametext_stub()
    {
        __asm {
            cmp eax, 0x1C
            jb normal
            je extra
            sub eax, 4
        normal:
            mov eax, [esp + eax + 0xC4 + 4]
            ret
        extra:
            mov eax, 14
            ret
        }
    }

    __declspec(naked) void handler_stub()
    {
        // eax = button id - 0x1B, esi = 0x0B (set by the game right before)
        __asm {
            cmp eax, 0x0B
            ja extra
            jmp dword ptr [eax * 4 + 0x48EF62] // original jump table
        extra:
            cmp eax, 12 // ptl
            jne skip
            mov esi, 12 // language index used by the code at 0x48ED81
            push 0x48ED81 // mov eax, esi / set font / load the language
            ret
        skip:
            push 0x48EDE5 // next button
            ret
        }
    }

    bool bytes_are(uintptr_t addr, std::initializer_list<uint8_t> expected)
    {
        const uint8_t* code = reinterpret_cast<const uint8_t*>(addr);
        size_t i = 0;
        for (uint8_t b : expected) {
            if (code[i++] != b)
                return false;
        }
        return true;
    }

    bool write_branch(uintptr_t site, uint8_t opcode, void* target, size_t size, const char* what)
    {
        uint8_t code[16];
        code[0] = opcode;
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (site + 5));
        std::memcpy(code + 1, &rel, 4);
        for (size_t i = 5; i < size; ++i)
            code[i] = 0x90;
        return Safety::write_bytes(site, code, size, what);
    }
}

void LangCompat::install_language_menu_entry()
{
    if (!bytes_are(kButtonIdRead, { 0x8B, 0x84, 0x04, 0x98, 0x00, 0x00, 0x00 })
        || !bytes_are(kNameTextRead, { 0x8B, 0x84, 0x04, 0xC4, 0x00, 0x00, 0x00 })
        || !bytes_are(kLoopCount - 4, { 0x83, 0x7C, 0x24, 0x24, 0x0B })
        || !bytes_are(kHandlerCheck, { 0x3B, 0xC6, 0x0F, 0x87, 0x97, 0x00, 0x00, 0x00, 0xFF, 0x24, 0x85, 0x62, 0xEF, 0x48, 0x00 })
        || !bytes_are(0x48ED80, { 0x5E, 0x8B, 0xC6 })) {
        std::cout << "[LangCompat] language menu doesn't look as expected, Português (Brasil) not added" << std::endl;
        return;
    }
    const uint8_t count = 12;
    if (write_branch(kButtonIdRead, 0xE8, reinterpret_cast<void*>(&buttonid_stub), 7, "language menu button ids")
        && write_branch(kNameTextRead, 0xE8, reinterpret_cast<void*>(&nametext_stub), 7, "language menu names")
        && write_branch(kHandlerCheck, 0xE9, reinterpret_cast<void*>(&handler_stub), 15, "language menu click")
        && Safety::write_bytes(kLoopCount, &count, 1, "language menu entry count"))
        std::cout << "[LangCompat] Português (Brasil) added to the language menu" << std::endl;
}
