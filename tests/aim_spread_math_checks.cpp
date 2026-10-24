#include "aim_spread_math.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int checks = 0, failures = 0;
constexpr double rad = .01745329251994329577, deg = 57.29577951308232088;
struct Vector { double x, y, z; };
void Expect(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
bool Near(double a, double b, double tolerance = .0002) { return std::abs(a-b) <= tolerance; }
// Independent literal AngleVectors reconstruction, including native punch roll.
Vector Shot(const AimAngles& effective, double x, double y) {
    const double p=effective.pitch*rad, yaw=effective.yaw*rad, r=effective.roll*rad;
    const double cp=std::cos(p), sp=std::sin(p), cy=std::cos(yaw), sy=std::sin(yaw);
    const double cr=std::cos(r), sr=std::sin(r);
    Vector v{cp*cy+x*(-sr*sp*cy+cr*sy)+y*(cr*sp*cy+sr*sy),
        cp*sy+x*(-sr*sp*sy-cr*cy)+y*(cr*sp*sy-sr*cy), -sp-x*sr*cp+y*cr*cp};
    const double n=std::hypot(std::hypot(v.x,v.y),v.z);
    return {v.x/n,v.y/n,v.z/n};
}
AimAngles Angles(Vector v) {
    return {static_cast<float>(-std::atan2(v.z,std::hypot(v.x,v.y))*deg),
        static_cast<float>(std::atan2(v.y,v.x)*deg),0};
}
double Error(Vector a, Vector b) {
    const double x=a.y*b.z-a.z*b.y, y=a.z*b.x-a.x*b.z, z=a.x*b.y-a.y*b.x;
    return std::atan2(std::hypot(std::hypot(x,y),z),a.x*b.x+a.y*b.y+a.z*b.z)*deg;
}
void Verify(const AimAngles& desired, const AimAngles& punch, double x, double y, const char* label) {
    const auto solution=AimSpreadCompensate(desired,punch,x,y);
    Expect(solution.valid,label);
    if (!solution.valid) return;
    Expect(std::abs(solution.command.pitch)<=89 && std::abs(solution.command.yaw)<=180 &&
        solution.command.roll==0,"command angles remain legal");
    Expect(solution.effective.roll==punch.roll,"native punch roll is retained");
    const double error=Error(Shot(solution.effective,x,y),Shot(desired,0,0));
    Expect(error<=.0002 && Near(error,solution.residualDegrees,.000001),"independent forward reconstruction matches desired direction and reported residual");
}
}

int main() {
    const auto zero=AimSpreadCompensate({12,179,0},{2,-5,17},0,0);
    Expect(zero.valid && Near(zero.command.pitch,10) && Near(zero.command.yaw,-176),"zero spread reduces to wrapped recoil compensation");
    Verify({-20,140,0},{2,-3,35},.7,-.9,"large airborne spread with nonzero native roll");
    Verify({35,-170,0},{-4,11,-70},-.8,1.2,"large spread with opposite roll and wrapped yaw");
    Verify({0,0,0},{0,0,0},2,2,"large tangent offset can still reach horizontal target");
    const auto left=AimSpreadCompensate({0,0,0},{},.4,.3);
    const auto right=AimSpreadCompensate({0,0,0},{},-.4,.3);
    Expect(left.valid && right.valid && Near(left.command.pitch,right.command.pitch) &&
        Near(left.command.yaw,-right.command.yaw),"horizontal spread reflection preserves pitch and reverses yaw");
    const auto down=AimSpreadCompensate({0,0,0},{},.4,-.3);
    Expect(left.valid && down.valid && Near(left.command.pitch,-down.command.pitch) &&
        Near(left.command.yaw,down.command.yaw),"vertical spread reflection reverses pitch and preserves yaw");
    Expect(!AimSpreadCompensate({89,0,0},{},1,0).valid,"lateral spread rejects unreachable near-pole target");
    Expect(!AimSpreadCompensate({90,0,0},{},0,0).valid,"zero spread cannot clamp a pole into legal pitch");
    Expect(!AimSpreadCompensate({88,0,0},{-4,0,0},0,0).valid,"recoil compensation beyond command pitch limit is rejected");
    Verify({90,43,0},{},0,-.1,"vertical target is accepted when spread makes it representable");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    Expect(!AimSpreadCompensate({nan,0,0},{},0,0).valid,"NaN desired pitch fails");
    Expect(!AimSpreadCompensate({0,inf,0},{},0,0).valid,"infinite desired yaw fails");
    Expect(!AimSpreadCompensate({},{0,0,nan},0,0).valid,"NaN native punch roll fails");
    Expect(!AimSpreadCompensate({},{},nan,0).valid && !AimSpreadCompensate({},{},0,inf).valid,"nonfinite spread offsets fail");
    Expect(!AimSpreadCompensate({0,0,1},{},0,0).valid,"nonzero desired roll is not treated as a world direction");
    Expect(!AimSpreadCompensate({91,0,0},{},0,0).valid,"invalid desired pitch fails");
    // Generate representable rays from legal commands before inversion. This
    // covers both pitch roots, nonzero punch, yaw rotation and airborne offsets.
    for (int p=-80;p<=80;p+=20) for (int yaw=-180;yaw<=180;yaw+=60)
        for (int roll=-150;roll<=150;roll+=75) for (int i=0;i<4;++i) {
            const double x=(i&1)?1.3:-.23, y=(i&2)?-.9:.31;
            const AimAngles punch{7,-13,static_cast<float>(roll)};
            const AimAngles effective{p+7.0f,yaw-13.0f,punch.roll};
            Verify(Angles(Shot(effective,x,y)),punch,x,y,"forward-generated reachable ray has an inverse");
        }
    std::printf("aim spread math: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
