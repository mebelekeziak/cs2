#include "grenade_draw.h"
#include <imgui.h>
#include <cmath>
#include <array>
#include <cstdio>
#include <limits>

namespace {
int failures=0;
void Expect(bool ok,const char* label) { if (!ok) { ++failures; std::printf("FAIL %s\n",label); } }
float Length(const std::vector<GrenadeEdge>& edges) {
    float total=0;
    for (const auto& e:edges) total+=std::hypot(e.a.x-e.b.x,e.a.y-e.b.y);
    return total;
}
}
int main() {
    GrenadeDisk one[]{{{0,0,0},1}};
    const auto circle=GrenadeDiskBoundary(one);
    Expect(circle.size()>=96 && std::abs(Length(circle)-6.28318f)<0.01f,"single disk boundary covers full circumference");
    GrenadeDisk overlap[]{{{0,0,0},1},{{1,0,0},1}};
    auto boundary=GrenadeDiskBoundary(overlap);
    Expect(std::abs(Length(boundary)-8.37758f)<0.02f,"overlapping disks omit internal arcs and retain concave union");
    bool exposed=true;
    for (const auto& e:boundary) {
        const float x=(e.a.x+e.b.x)*0.5f,y=(e.a.y+e.b.y)*0.5f;
        exposed &= std::hypot(x,y)>0.998f && std::hypot(x-1,y)>0.998f;
    }
    Expect(exposed,"no interior seams between active flame cells");
    GrenadeDisk contained[]{{{0,0,0},2},{{0,0,0},1},{{0,0,0},2}};
    Expect(std::abs(Length(GrenadeDiskBoundary(contained))-12.56637f)<0.02f,"duplicate and enclosed disks do not double outlines");
    GrenadeDisk separated[]{{{0,0,0},1},{{4,0,0},1}};
    Expect(std::abs(Length(GrenadeDiskBoundary(separated))-12.56637f)<0.02f,"disconnected fire islands retained");
    GrenadeDisk stacked[]{{{0,0,0},1},{{0,0,20},1}};
    Expect(std::abs(Length(GrenadeDiskBoundary(stacked))-12.56637f)<0.02f,"distinct floor levels retained");
    one[0].radius=std::numeric_limits<float>::quiet_NaN();
    Expect(GrenadeDiskBoundary(one).empty(),"nonfinite radius rejected");
    std::array<bool,1024> columns{};
    Expect(GrenadeSmokeBoundary(columns,{},20).empty(),"empty smoke volume has no aura");
    columns[16+32*16]=true;
    auto footprint=GrenadeSmokeBoundary(columns,{10,20,30},20);
    Expect(footprint.size()==4 && footprint[0].a.x==10 && footprint[0].a.y==20 && footprint[0].a.z==30,
        "smoke footprint uses native grid transform at detonation plane");
    columns[17+32*16]=true;
    Expect(GrenadeSmokeBoundary(columns,{},20).size()==6,"adjacent smoke cells remove internal seams");
    columns[20+32*16]=true;
    Expect(GrenadeSmokeBoundary(columns,{},20).size()==10,"smoke footprint retains separated occupied columns");
    columns.fill(true); columns[16+32*16]=false;
    Expect(GrenadeSmokeBoundary(columns,{},20).size()==132,"smoke footprint preserves exterior perimeter and interior holes");
    EspMatrix4x4 m{}; m.m[0][0]=m.m[1][1]=m.m[2][2]=m.m[3][3]=1;
    float p[4]{};
    Expect(GrenadeProjectEdge({{-2,0,0},{2,0,0}},m,100,100,p) && std::abs(p[0])<0.01f && std::abs(p[2]-100)<0.01f,"viewport crossing clips to viewport");
    Expect(!GrenadeProjectEdge({{2,2,0},{3,3,0}},m,100,100,p),"fully offscreen edge rejected");
    m.m[3][3]=0; m.m[3][2]=1;
    Expect(!GrenadeProjectEdge({{0,0,-1},{0,0,-2}},m,100,100,p),"behind camera rejected");
    Expect(GrenadeProjectEdge({{0,0,-1},{0.5f,0,1}},m,100,100,p),"near plane crossing clipped");
    Expect(!GrenadeProjectEdge({},m,0,100,p),"invalid viewport rejected");
    Expect(!GrenadeDraw({},m,100,100,{}),"draw safe without ImGui context");
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize={100,100}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
    unsigned char* pixels=nullptr; int w=0,h=0; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    ImGui::NewFrame();
    m={}; m.m[0][0]=m.m[1][1]=m.m[3][3]=1;
    GrenadeArea area; area.edges.push_back({{-0.5f,0,0},{0.5f,0,0}});
    GrenadeSettings settings;
    Expect(!GrenadeDraw(std::span(&area,1),m,100,100,settings),"disabled aura draws nothing");
    settings.smokeAura=true;
    Expect(GrenadeDraw(std::span(&area,1),m,100,100,settings)==1,"smoke aura draws independent of removal");
    area.kind=GrenadeAreaKind::Molotov;
    Expect(!GrenadeDraw(std::span(&area,1),m,100,100,settings),"fire toggle independent of smoke");
    settings.molotovAura=true; settings.molotovColor[3]=0;
    Expect(!GrenadeDraw(std::span(&area,1),m,100,100,settings),"transparent tint suppresses all glow layers");
    ImGui::Render(); ImGui::DestroyContext();
    std::printf("Grenade draw failures: %d\n",failures);
    return failures?1:0;
}
