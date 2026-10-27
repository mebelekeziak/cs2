#include "weapon_icons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
int failures=0, checks=0;
void Expect(bool value,const char* message) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n",message); }
}
std::vector<ImVec2> Shape(ImDrawList& draw,std::uint16_t definition,float height=20) {
    const int begin=draw.VtxBuffer.Size;
    Expect(WeaponIconDraw(draw,{200,150},definition,height,IM_COL32(40,210,180,190)),"known glyph draws");
    std::vector<ImVec2> result;
    for (int i=begin;i<draw.VtxBuffer.Size;++i) result.push_back(draw.VtxBuffer[i].pos);
    return result;
}
bool Same(const std::vector<ImVec2>& a,const std::vector<ImVec2>& b) {
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();++i)
        if (a[i].x!=b[i].x || a[i].y!=b[i].y) return false;
    return true;
}
std::uint64_t BitmapHash(const ImFontGlyph& glyph,const unsigned char* pixels,int width,int height) {
    const int left=static_cast<int>(std::lround(glyph.U0*width)),top=static_cast<int>(std::lround(glyph.V0*height));
    const int right=static_cast<int>(std::lround(glyph.U1*width)),bottom=static_cast<int>(std::lround(glyph.V1*height));
    std::uint64_t result=1469598103934665603ULL;
    for (int y=top;y<bottom;++y) for (int x=left;x<right;++x)
        result=(result^pixels[(y*width+x)*4+3])*1099511628211ULL;
    return result;
}
unsigned AlphaAt(const ImFontGlyph& glyph,const unsigned char* pixels,int width,int height,float x,float y) {
    const int px=std::clamp(static_cast<int>((glyph.U0+(glyph.U1-glyph.U0)*x)*width),0,width-1);
    const int py=std::clamp(static_cast<int>((glyph.V0+(glyph.V1-glyph.V0)*y)*height),0,height-1);
    return pixels[(py*width+px)*4+3];
}
}

int main() {
    Expect(WeaponIconName(0)==nullptr && WeaponIconName(65535)==nullptr,"unknown definitions have no label");
    Expect(std::strcmp(WeaponIconName(7),"AK-47")==0 && std::strcmp(WeaponIconName(49),"C4")==0,
        "item definitions identify AK and C4 independently");
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize={400,300}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
    unsigned char* pixels=nullptr; int width=0,height=0;
    WeaponIconConfigure();
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    ImFont* icons=nullptr;
    for (const auto& source : io.Fonts->Sources)
        if (std::strcmp(source.Name,"CS2 installed HUD icons")==0) icons=source.DstFont;
    Expect(icons!=nullptr,"installed HUD font imported into atlas");
    if (!icons) { ImGui::DestroyContext(); return 1; }
    const auto* akGlyph=icons->FindGlyphNoFallback(0xE007);
    const auto* m4Glyph=icons->FindGlyphNoFallback(0xE010);
    const auto* awpGlyph=icons->FindGlyphNoFallback(0xE009);
    const auto* c4Glyph=icons->FindGlyphNoFallback(0xE031);
    const auto* plantedGlyph=icons->FindGlyphNoFallback(0xEFFF);
    Expect(akGlyph && m4Glyph && awpGlyph && c4Glyph && plantedGlyph,"source HUD silhouettes have dedicated glyphs");
    if (akGlyph && m4Glyph && awpGlyph && c4Glyph && plantedGlyph) {
        Expect(BitmapHash(*akGlyph,pixels,width,height)!=BitmapHash(*m4Glyph,pixels,width,height) &&
            BitmapHash(*m4Glyph,pixels,width,height)!=BitmapHash(*awpGlyph,pixels,width,height),
            "actual atlas raster distinguishes AK, M4 and AWP artwork");
        Expect(BitmapHash(*c4Glyph,pixels,width,height)!=BitmapHash(*plantedGlyph,pixels,width,height),
            "dropped/carried C4 and planted C4 retain distinct installed artwork");
        Expect((awpGlyph->X1-awpGlyph->X0)/(awpGlyph->Y1-awpGlyph->Y0)>4.5f,
            "AWP source aspect remains long rather than being stretched into reserved box");
        // Independent installed AK SVG oracle: its trigger-hole center is
        // (34,14), while (45,22) lies in its curved magazine. Actual ink bounds
        // are [0.667,4.431,88.334,29.984]. Check raster coverage, not quad count.
        Expect(AlphaAt(*akGlyph,pixels,width,height,(34-.667f)/87.667f,(14-4.431f)/25.553f)<30,
            "original AK trigger hole stays transparent after even-odd conversion");
        Expect(AlphaAt(*akGlyph,pixels,width,height,(45-.667f)/87.667f,(22-4.431f)/25.553f)>220,
            "original AK curved magazine stays filled in actual atlas raster");
    }
    ImGui::NewFrame();
    auto& draw=*ImGui::GetBackgroundDrawList();
    constexpr std::uint16_t definitions[]{1,2,3,4,7,8,9,10,11,13,14,16,17,19,23,24,25,26,27,28,29,
        30,31,32,33,34,35,36,38,39,40,41,42,43,44,45,46,47,48,49,59,60,61,63,64,
        500,503,505,506,507,508,509,512,514,515,516,517,518,519,520,521,522,523,525,526,WeaponIconPlantedC4};
    for (auto definition : definitions) {
        Expect(WeaponIconName(definition)!=nullptr && WeaponIconAvailable(definition),"supported weapon has a label and actual atlas glyph");
        const auto shape=Shape(draw,definition);
        Expect(!shape.empty(),"supported weapon emits geometry");
        bool bounded=true;
        for (const auto point : shape) bounded &= std::isfinite(point.x) && std::isfinite(point.y) &&
            std::abs(point.x-200)<=30.001f && std::abs(point.y-150)<=10.001f;
        Expect(bounded,"glyph fits documented finite centered bounds");
    }
    const auto ak=Shape(draw,7), m4=Shape(draw,16), suppressed=Shape(draw,60), awp=Shape(draw,9);
    Expect(!Same(ak,m4) && !Same(m4,suppressed) && !Same(m4,awp),
        "AK magazine, M4 carry handle, suppressor and scoped sniper differ");
    Expect(!Same(Shape(draw,4),Shape(draw,61)),"pistol suppressor changes geometry");
    Expect(!Same(Shape(draw,17),Shape(draw,19)) && !Same(Shape(draw,19),Shape(draw,26)),
        "MAC-10, P90 and Bizon silhouettes differ");
    const auto bomb=Shape(draw,49);
    bool compact=true;
    for (const auto point : bomb) compact &= std::abs(point.x-200)<=13;
    Expect(compact,"C4 fits compact carrier badge width");
    const int grenadeStart=draw.VtxBuffer.Size;
    Shape(draw,43);
    bool translucent=false,opaque=false;
    for (int i=grenadeStart;i<draw.VtxBuffer.Size;++i) {
        const auto alpha=(draw.VtxBuffer[i].col>>IM_COL32_A_SHIFT)&255;
        translucent |= alpha==76; opaque |= alpha==190;
    }
    Expect(translucent && opaque,"installed flashbang 0.4-opacity layer retained separately");
    for (const float size : {4.0f,8.0f,128.0f}) {
        const ImDrawListFlags flags=draw.Flags;
        const auto shape=Shape(draw,49,size);
        bool bounded=true;
        for (const auto point : shape) bounded &= std::abs(point.x-200)<=size*.65f+.001f && std::abs(point.y-150)<=size*.5f+.001f;
        Expect(bounded,"extreme supported C4 sizes fit reserved badge");
        for (const auto definition : definitions) {
            const auto glyph=Shape(draw,definition,size);
            bool fits=true;
            for (const auto point : glyph) fits &= std::abs(point.x-200)<=size*1.5f+.001f && std::abs(point.y-150)<=size*.5f+.001f;
            if (!fits) std::printf("Bounds definition=%u height=%.1f\n",definition,size);
            Expect(fits,"all weapon silhouettes fit bounds at smallest/largest size");
        }
        Expect(draw.Flags==flags,"tiny glyph rendering restores draw list flags");
    }
    const int vertices=draw.VtxBuffer.Size;
    Expect(!WeaponIconDraw(draw,{200,150},65535,20,IM_COL32_WHITE),"unknown weapon omitted");
    Expect(!WeaponIconDraw(draw,{200,150},7,20,IM_COL32(255,255,255,0)),"transparent icon omitted");
    for (float size : {-1.0f,0.0f,3.0f,129.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        Expect(!WeaponIconDraw(draw,{200,150},7,size,IM_COL32_WHITE),"invalid size omitted");
    Expect(!WeaponIconDraw(draw,{std::numeric_limits<float>::quiet_NaN(),150},7,20,IM_COL32_WHITE),"nonfinite center omitted");
    Expect(!WeaponIconDraw(draw,{1.0e20f,150},7,20,IM_COL32_WHITE),"extreme center omitted");
    Expect(draw.VtxBuffer.Size==vertices,"rejected inputs add no geometry");
    bool tint=true;
    for (const auto& vertex : draw.VtxBuffer)
        if (vertex.col & IM_COL32_A_MASK) tint &= (vertex.col & ~IM_COL32_A_MASK)==(IM_COL32(40,210,180,190) & ~IM_COL32_A_MASK);
    Expect(tint,"all glyph geometry uses requested RGB");
    ImGui::Render(); ImGui::DestroyContext();
    Expect(!WeaponIconAvailable(7),"font unavailable after context destruction");
    ImGui::CreateContext();
    auto& fresh=ImGui::GetIO(); fresh.DisplaySize={400,300}; fresh.DeltaTime=1.0f/60; fresh.IniFilename=nullptr;
    fresh.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    Expect(!WeaponIconAvailable(7),"new default atlas does not retain destroyed icon pointers");
    ImGui::NewFrame();
    auto& missing=*ImGui::GetBackgroundDrawList();
    const int emptyVertices=missing.VtxBuffer.Size;
    Expect(!WeaponIconDraw(missing,{200,150},7,20,IM_COL32_WHITE) && missing.VtxBuffer.Size==emptyVertices,
        "missing imported font omits glyph with no fabricated fallback");
    ImGui::Render(); ImGui::DestroyContext();
    ImGui::CreateContext();
    auto& rebuilt=ImGui::GetIO(); rebuilt.DisplaySize={400,300}; rebuilt.DeltaTime=1.0f/60; rebuilt.IniFilename=nullptr;
    WeaponIconConfigure(); WeaponIconConfigure();
    rebuilt.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    Expect(WeaponIconAvailable(7) && WeaponIconAvailable(WeaponIconPlantedC4),"new atlas imports original icons again");
    ImGui::DestroyContext();
    std::printf("Weapon icons: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
