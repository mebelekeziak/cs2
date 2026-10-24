#pragma once
#include "silent_aim_math.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

// Test-only independent model of the installed verified SHA1/ran1 contracts.
namespace spread_replay_reference {
inline constexpr double rad=.01745329251994329577,deg=57.29577951308232088;
inline std::uint32_t Swap(std::uint32_t n) noexcept {
    return (n>>24)|((n>>8)&0xff00u)|((n<<8)&0xff0000u)|(n<<24);
}
inline float Normalize(float a) noexcept {
    while (a>180) a-=360;
    while (a< -180) a+=360;
    return a;
}
// Independent one-block SHA-1 for exactly the native 12-byte LE float/float/int
// payload. No native code, mutable game RNG or cryptographic library is used.
inline std::uint32_t Seed(const AimAngles& angles,int tick) noexcept {
    const float pitch=std::floor(Normalize(angles.pitch)*2.0f)*.5f;
    const float yaw=std::floor(Normalize(angles.yaw)*2.0f)*.5f;
    std::array<std::uint32_t,80> w{};
    w[0]=Swap(std::bit_cast<std::uint32_t>(pitch));
    w[1]=Swap(std::bit_cast<std::uint32_t>(yaw));
    w[2]=Swap(static_cast<std::uint32_t>(tick)); w[3]=0x80000000u; w[15]=96;
    for (unsigned i=16;i<80;++i) w[i]=std::rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    std::uint32_t a=0x67452301u,b=0xefcdab89u,c=0x98badcfeu,d=0x10325476u,e=0xc3d2e1f0u;
    for (unsigned i=0;i<80;++i) {
        const auto f=i<20?((b&c)|(~b&d)):i<40?(b^c^d):i<60?((b&c)|(b&d)|(c&d)):(b^c^d);
        const auto k=i<20?0x5a827999u:i<40?0x6ed9eba1u:i<60?0x8f1bbcdcu:0xca62c1d6u;
        const auto t=std::rotl(a,5)+f+e+k+w[i]; e=d; d=c; c=std::rotl(b,30); b=a; a=t;
    }
    return Swap(a+0x67452301u)+1u;
}
struct Random {
    std::int64_t state=1; int index=0; std::array<int,32> shuffle{};
    explicit Random(std::uint32_t seed) noexcept {
        const auto signedSeed=std::bit_cast<std::int32_t>(seed);
        state=signedSeed==-2147483647-1?1:std::max<std::int64_t>(1,std::abs(static_cast<std::int64_t>(signedSeed)));
        for (int j=39;j>=0;--j) { Step(); if (j<32) shuffle[j]=static_cast<int>(state); }
        index=shuffle[0];
    }
    void Step() noexcept { state=(state*16807)%2147483647; }
    float Float(float high) noexcept {
        Step(); const auto slot=index/67108864; index=shuffle[slot]; shuffle[slot]=static_cast<int>(state);
        const float unit=std::min(static_cast<float>(index)*0x1p-31f,0.9999998807907104f);
        return unit*high;
    }
};
struct Vector { double x,y,z; };
inline Vector Ray(const AimAngles& a,double x=0,double y=0) noexcept {
    const double p=a.pitch*rad,yaw=a.yaw*rad;
    return {std::cos(p)*std::cos(yaw)+x*std::sin(yaw)+y*std::sin(p)*std::cos(yaw),
        std::cos(p)*std::sin(yaw)-x*std::cos(yaw)+y*std::sin(p)*std::sin(yaw),-std::sin(p)+y*std::cos(p)};
}
inline double Error(Vector a,Vector b) noexcept {
    const auto cross=Vector{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    return std::atan2(std::hypot(std::hypot(cross.x,cross.y),cross.z),a.x*b.x+a.y*b.y+a.z*b.z)*deg;
}
inline void Offset(std::uint32_t seed,float inaccuracy,float spread,double& x,double& y) noexcept {
    Random random(seed);
    const float radiusA=random.Float(1),angleA=random.Float(6.283185482025146484375f);
    const float angleB=random.Float(6.283185482025146484375f),radiusB=random.Float(1);
    const float a=radiusA*inaccuracy,b=radiusB*spread;
    x=std::cos(angleA)*a+std::cos(angleB)*b;
    y=std::sin(angleA)*a+std::sin(angleB)*b;
}
inline AimAngles Effective(const AimAngles& command,const AimAngles& punch) noexcept {
    return {command.pitch+punch.pitch,command.yaw+punch.yaw,punch.roll};
}
inline Vector ModelRay(const AimAngles& effective,int tick,float inaccuracy,float spread,std::uint32_t* outSeed=nullptr) noexcept {
    const auto seed=Seed(effective,tick);double x=0,y=0;Offset(seed,inaccuracy,spread,x,y);
    if (outSeed) *outSeed=seed;
    return Ray(effective,x,y);
}
} // namespace spread_replay_reference
