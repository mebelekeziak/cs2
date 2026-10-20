#include "grenade_draw.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr float pi = 3.14159265358979323846f;
bool Valid(const EspVec3& p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x) < 1000000 && std::abs(p.y) < 1000000 && std::abs(p.z) < 1000000;
}
struct Clip { float x, y, w; };
Clip Transform(const EspVec3& p, const EspMatrix4x4& m) noexcept {
    return {m.m[0][0]*p.x+m.m[0][1]*p.y+m.m[0][2]*p.z+m.m[0][3],
        m.m[1][0]*p.x+m.m[1][1]*p.y+m.m[1][2]*p.z+m.m[1][3],
        m.m[3][0]*p.x+m.m[3][1]*p.y+m.m[3][2]*p.z+m.m[3][3]};
}
}

std::vector<GrenadeEdge> GrenadeDiskBoundary(std::span<const GrenadeDisk> disks) {
    std::vector<GrenadeEdge> result;
    if (disks.size() > 64) return result;
    for (std::size_t i=0; i<disks.size(); ++i) {
        const auto& disk=disks[i];
        if (!Valid(disk.center) || !std::isfinite(disk.radius) || disk.radius<=0 || disk.radius>1024) return {};
    }
    for (std::size_t i=0; i<disks.size(); ++i) {
        const auto& a=disks[i];
        std::vector<float> cuts{0, 2*pi};
        bool covered=false;
        for (std::size_t j=0; j<disks.size(); ++j) {
            if (i==j) continue;
            const auto& b=disks[j];
            // Distinct levels must retain their own outlines.
            if (std::abs(a.center.z-b.center.z)>std::min(a.radius,b.radius)) continue;
            const float dx=b.center.x-a.center.x, dy=b.center.y-a.center.y;
            const float d=std::hypot(dx,dy);
            if (d+a.radius<=b.radius && (d>0 || a.radius<b.radius || j<i)) { covered=true; break; }
            if (d<=std::abs(a.radius-b.radius) || d>=a.radius+b.radius) continue;
            const float heading=std::atan2(dy,dx);
            const float half=std::acos(std::clamp((a.radius*a.radius+d*d-b.radius*b.radius)/(2*a.radius*d),-1.0f,1.0f));
            for (float angle : {heading-half,heading+half}) {
                angle=std::fmod(angle+2*pi,2*pi);
                cuts.push_back(angle);
            }
        }
        if (covered) continue;
        std::sort(cuts.begin(),cuts.end());
        const auto point=[&](float t) { return EspVec3{a.center.x+a.radius*std::cos(t),a.center.y+a.radius*std::sin(t),a.center.z}; };
        for (std::size_t k=1; k<cuts.size(); ++k) {
            const float start=cuts[k-1], end=cuts[k];
            if (end-start<0.00001f) continue;
            const auto middle=point((start+end)*0.5f);
            bool hidden=false;
            for (std::size_t j=0; j<disks.size(); ++j) {
                if (j==i) continue;
                const auto& b=disks[j];
                if (std::abs(a.center.z-b.center.z)>std::min(a.radius,b.radius)) continue;
                if (std::hypot(middle.x-b.center.x,middle.y-b.center.y)<b.radius-0.0001f) { hidden=true; break; }
            }
            if (hidden) continue;
            const int steps=std::max(1,static_cast<int>(std::ceil((end-start)/(2*pi/96))));
            EspVec3 previous=point(start);
            for (int n=1; n<=steps; ++n) {
                auto next=point(start+(end-start)*static_cast<float>(n)/steps);
                result.push_back({previous,next}); previous=next;
            }
        }
    }
    return result;
}

std::vector<GrenadeEdge> GrenadeSmokeBoundary(std::span<const bool, 1024> columns,
    const EspVec3& origin, float cellSize) {
    std::vector<GrenadeEdge> result;
    if (!Valid(origin) || !std::isfinite(cellSize) || cellSize<=0 || cellSize>1024) return result;
    const auto occupied=[&](int x,int y) { return x>=0 && x<32 && y>=0 && y<32 && columns[x+32*y]; };
    for (int y=0; y<32; ++y) for (int x=0; x<32; ++x) {
        if (!occupied(x,y)) continue;
        const float left=origin.x+(x-16)*cellSize, bottom=origin.y+(y-16)*cellSize;
        const EspVec3 a{left,bottom,origin.z}, b{left+cellSize,bottom,origin.z};
        const EspVec3 c{left+cellSize,bottom+cellSize,origin.z}, d{left,bottom+cellSize,origin.z};
        if (!occupied(x,y-1)) result.push_back({a,b});
        if (!occupied(x+1,y)) result.push_back({b,c});
        if (!occupied(x,y+1)) result.push_back({c,d});
        if (!occupied(x-1,y)) result.push_back({d,a});
    }
    return result;
}

bool GrenadeProjectEdge(const GrenadeEdge& edge, const EspMatrix4x4& matrix,
    float width, float height, float (&screen)[4]) noexcept {
    screen[0]=screen[1]=screen[2]=screen[3]=0;
    if (!Valid(edge.a) || !Valid(edge.b) || !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0) return false;
    auto a=Transform(edge.a,matrix), b=Transform(edge.b,matrix);
    for (float f : {a.x,a.y,a.w,b.x,b.y,b.w}) if (!std::isfinite(f)) return false;
    // Homogeneous clipping, including viewport edges, keeps camera crossings
    // bounded instead of drawing enormous screen-space segments.
    float first=0, last=1;
    const float av[]{a.w-0.01f,a.w+a.x,a.w-a.x,a.w+a.y,a.w-a.y};
    const float bv[]{b.w-0.01f,b.w+b.x,b.w-b.x,b.w+b.y,b.w-b.y};
    for (int plane=0; plane<5; ++plane) {
        if (av[plane]<0 && bv[plane]<0) return false;
        if ((av[plane]<0)!=(bv[plane]<0)) {
            const float t=av[plane]/(av[plane]-bv[plane]);
            if (av[plane]<0) first=std::max(first,t); else last=std::min(last,t);
        }
    }
    if (first>last) return false;
    const auto project=[&](float t, float& x, float& y) {
        const float w=a.w+(b.w-a.w)*t;
        x=(1+(a.x+(b.x-a.x)*t)/w)*width*0.5f;
        y=(1-(a.y+(b.y-a.y)*t)/w)*height*0.5f;
    };
    project(first,screen[0],screen[1]); project(last,screen[2],screen[3]);
    for (float f : screen) if (!std::isfinite(f)) return false;
    return true;
}

std::size_t GrenadeDraw(std::span<const GrenadeArea> areas, const EspMatrix4x4& matrix,
    float width, float height, const GrenadeSettings& input) {
    if (!ImGui::GetCurrentContext()) return 0;
    const auto settings=GrenadeSanitizeSettings(input);
    auto* draw=ImGui::GetBackgroundDrawList();
    std::size_t drawn=0;
    draw->PushClipRect({0,0},{std::max(0.0f,width),std::max(0.0f,height)},true);
    for (const auto& area : areas) {
        const bool smoke=area.kind==GrenadeAreaKind::Smoke;
        if (!(smoke?settings.smokeAura:settings.molotovAura)) continue;
        const float* color=smoke?settings.smokeColor:settings.molotovColor;
        if (color[3]<=0) continue;
        for (const auto& edge : area.edges) {
            float p[4]{};
            if (!GrenadeProjectEdge(edge,matrix,width,height,p)) continue;
            const ImVec2 a{p[0],p[1]}, b{p[2],p[3]};
            for (int layer=3; layer>=0; --layer) {
                const float alpha=layer==0?color[3]:color[3]*(0.065f+0.025f*(3-layer));
                draw->AddLine(a,b,ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],alpha}),settings.auraWidth+layer*3.0f);
            }
            ++drawn;
        }
    }
    draw->PopClipRect();
    return drawn;
}
