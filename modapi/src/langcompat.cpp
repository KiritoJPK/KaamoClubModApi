#include "langcompat.h"
#include "offset.h"
#include <Game/structs.h>
#include <cstddef>
#include <iostream>

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
