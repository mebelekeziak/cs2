#include "weapon_icons.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef CS2_WEAPON_ICON_FONT
#define CS2_WEAPON_ICON_FONT "logs/weapon-icons/cs2-hud-icons.ttf"
#endif

namespace {
struct Entry { std::uint16_t id; const char* name; };
// Labels match the installed item catalog; artwork is imported locally from
// installed panorama/images/icons/equipment/*.vsvg_c by import_weapon_icons.py.
constexpr Entry entries[]{
    {1,"Desert Eagle"}, {2,"Dual Berettas"},
    {3,"Five-SeveN"}, {4,"Glock-18"},
    {7,"AK-47"}, {8,"AUG"}, {9,"AWP"},
    {10,"FAMAS"}, {11,"G3SG1"}, {13,"Galil AR"},
    {14,"M249"}, {16,"M4A4"}, {17,"MAC-10"},
    {19,"P90"}, {23,"MP5-SD"}, {24,"UMP-45"},
    {25,"XM1014"}, {26,"PP-Bizon"}, {27,"MAG-7"},
    {28,"Negev"}, {29,"Sawed-Off"},
    {30,"Tec-9"}, {31,"Zeus x27"}, {32,"P2000"},
    {33,"MP7"}, {34,"MP9"}, {35,"Nova"},
    {36,"P250"}, {38,"SCAR-20"}, {39,"SG 553"},
    {40,"SSG 08"}, {41,"Knife"}, {42,"Knife"},
    {43,"Flashbang"}, {44,"HE Grenade"},
    {45,"Smoke"}, {46,"Molotov"},
    {47,"Decoy"}, {48,"Incendiary"}, {49,"C4"},
    {59,"Knife"}, {60,"M4A1-S"}, {61,"USP-S"},
    {63,"CZ75-Auto"}, {64,"R8 Revolver"},
    {500,"Bayonet"}, {503,"Classic Knife"},
    {505,"Flip Knife"}, {506,"Gut Knife"},
    {507,"Karambit"}, {508,"M9 Bayonet"},
    {509,"Huntsman Knife"}, {512,"Falchion Knife"},
    {514,"Bowie Knife"}, {515,"Butterfly Knife"},
    {516,"Shadow Daggers"}, {517,"Paracord Knife"},
    {518,"Survival Knife"}, {519,"Ursus Knife"},
    {520,"Navaja Knife"}, {521,"Nomad Knife"},
    {522,"Stiletto Knife"}, {523,"Talon Knife"},
    {525,"Skeleton Knife"}, {526,"Kukri Knife"}, {WeaponIconPlantedC4,"Planted C4"}
};
constexpr char fontName[] = "CS2 installed HUD icons";
constexpr ImWchar iconRange[]{0xE000,0xEFFF,0};
const Entry* Find(std::uint16_t id) noexcept {
    for (const auto& entry : entries) if (entry.id == id) return &entry;
    return nullptr;
}
ImWchar Codepoint(std::uint16_t id) noexcept {
    return id==WeaponIconPlantedC4?0xEFFF:static_cast<ImWchar>(0xE000+id);
}
ImFont* Font() noexcept {
    if (!ImGui::GetCurrentContext()) return nullptr;
    // Resolve against the current context: atlas destruction never leaves a
    // retained font pointer behind.
    for (const auto& source : ImGui::GetIO().Fonts->Sources)
        if (std::strcmp(source.Name,fontName)==0) return source.DstFont;
    return nullptr;
}
const ImFontGlyph* Glyph(std::uint16_t id,ImFont*& font) noexcept {
    font=nullptr;
    if (!Find(id) || !ImGui::GetCurrentContext() || !ImGui::GetIO().Fonts->IsBuilt()) return nullptr;
    font=Font();
    return font?font->FindGlyphNoFallback(Codepoint(id)):nullptr;
}
}

const char* WeaponIconName(std::uint16_t definition) noexcept {
    const auto* entry=Find(definition);
    return entry?entry->name:nullptr;
}

void WeaponIconConfigure() {
    if (!ImGui::GetCurrentContext()) return;
    auto& atlas=*ImGui::GetIO().Fonts;
    if (atlas.Locked || atlas.IsBuilt() || Font()) return;
    wchar_t path[2048]{};
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,CS2_WEAPON_ICON_FONT,-1,path,2048)) return;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file,&length) || length.QuadPart<1000 || length.QuadPart>8*1024*1024) {
        CloseHandle(file); return;
    }
    void* data=ImGui::MemAlloc(static_cast<std::size_t>(length.QuadPart));
    DWORD read=0;
    const bool complete=data && ReadFile(file,data,static_cast<DWORD>(length.QuadPart),&read,nullptr) &&
        read==static_cast<DWORD>(length.QuadPart);
    CloseHandle(file);
    if (!complete) { ImGui::MemFree(data); return; }
    // Keep the normal default face first in otherwise empty/headless atlases.
    if (atlas.Fonts.empty()) ImGui::GetIO().FontDefault=atlas.AddFontDefault();
    ImFontConfig config{};
    strcpy_s(config.Name,fontName);
    config.FontDataOwnedByAtlas=true;
    config.OversampleH=config.OversampleV=2;
    atlas.AddFontFromMemoryTTF(data,static_cast<int>(length.QuadPart),64,&config,iconRange);
}

bool WeaponIconAvailable(std::uint16_t definition) noexcept {
    ImFont* font=nullptr;
    const auto* glyph=Glyph(definition,font);
    return glyph && glyph->Visible && glyph->X1>glyph->X0 && glyph->Y1>glyph->Y0;
}

bool WeaponIconDraw(ImDrawList& draw, ImVec2 center, std::uint16_t definition,
                    float height, ImU32 color) {
    if (!(color & IM_COL32_A_MASK) || !std::isfinite(height) || height<4 || height>128 ||
        !std::isfinite(center.x) || !std::isfinite(center.y) ||
        std::abs(center.x)>1000000 || std::abs(center.y)>1000000) return false;
    ImFont* font=nullptr;
    const auto* glyph=Glyph(definition,font);
    if (!glyph || !glyph->Visible) return false;
    const float width=glyph->X1-glyph->X0, inkHeight=glyph->Y1-glyph->Y0;
    if (width<=0 || inkHeight<=0) return false;
    const float boxWidth=(definition==49 || definition==WeaponIconPlantedC4?1.3f:3.0f)*height;
    const float scale=std::min(height/inkHeight,boxWidth/width);
    const ImVec2 origin{center.x-(glyph->X0+glyph->X1)*.5f*scale,
                        center.y-(glyph->Y0+glyph->Y1)*.5f*scale};
    const int before=draw.VtxBuffer.Size;
    const auto layer=[&](ImWchar code,ImU32 tint) {
        const auto* ink=font->FindGlyphNoFallback(code);
        if (!ink || !ink->Visible || !(tint&IM_COL32_A_MASK)) return;
        // Direct glyph quads avoid AddText's integer origin rounding, retaining
        // symmetric bounds and centering even in small C4 carrier badges.
        draw.AddImage(font->ContainerAtlas->TexID,
            {origin.x+ink->X0*scale,origin.y+ink->Y0*scale},
            {origin.x+ink->X1*scale,origin.y+ink->Y1*scale},
            {ink->U0,ink->V0},{ink->U1,ink->V1},tint);
    };
    const float partial=definition==43?.4f:definition==46?.6f:definition==47?.851f:0;
    if (partial>0) {
        const unsigned alpha=static_cast<unsigned>((color>>IM_COL32_A_SHIFT)&255);
        const auto faded=(color&~IM_COL32_A_MASK) |
            (static_cast<unsigned>(std::lround(alpha*partial))<<IM_COL32_A_SHIFT);
        layer(static_cast<ImWchar>(0xE800+definition),faded);
        layer(static_cast<ImWchar>(0xE400+definition),color);
    } else layer(Codepoint(definition),color);
    return draw.VtxBuffer.Size>before;
}
