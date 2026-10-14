#include "world_lighting.h"
#include "world_lighting_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <limits>
namespace {
using namespace world_lighting_offsets;
constexpr std::uintptr_t base=0x10000000;
unsigned checks=0,failures=0;
std::array<unsigned char,SubLightStride*MaxSubLights> records{};
std::array<unsigned char,0x120> source{};
std::array<unsigned char,AreaStride> areaRecord{};
int requestedCount=1;
bool invalidLast=false,argsPreserved=false;
void* matrixSentinel=reinterpret_cast<void*>(0x12345678);
HANDLE originalEntered=nullptr,originalResume=nullptr;
void Check(bool good,const char* name) { ++checks; if (!good) { ++failures; std::printf("FAIL %s\n",name); } }
template<class T> void Put(void* p,std::size_t offset,T v) { std::memcpy(static_cast<unsigned char*>(p)+offset,&v,sizeof(v)); }
float Channel(int record,unsigned channel) { float v=0;std::memcpy(&v,records.data()+record*SubLightStride+Radiance+channel*4,4);return v; }
bool Near(float a,float b) {return std::abs(a-b)<0.00001f;}
int NativeGenerator(void* out,void* matrices,void* input,bool shadows,float scale) {
    argsPreserved=out==records.data() && matrices==matrixSentinel && input==source.data() && shadows && scale==1.75f;
    if (originalEntered) {SetEvent(originalEntered);WaitForSingleObject(originalResume,INFINITE);}
    // Native generator also updates derived source geometry. Forwarding must
    // retain those native changes, while our modifier touches output RGB only.
    Put(input,0xD0,std::uint32_t{0xABCDEF01});
    for(int i=0;i<requestedCount && i<static_cast<int>(MaxSubLights);++i) {
        float rgb[]{2.0f+i,3,4};
        if(invalidLast && i==requestedCount-1) rgb[2]=std::numeric_limits<float>::quiet_NaN();
        std::memcpy(static_cast<unsigned char*>(out)+i*SubLightStride+Radiance,rgb,sizeof(rgb));
    }
    return requestedCount;
}
std::uintptr_t NativeArea(void* context,void* out,int index) {
    argsPreserved=context==source.data() && out==areaRecord.data() && index==13;
    if(originalEntered) {SetEvent(originalEntered);WaitForSingleObject(originalResume,INFINITE);}
    const float rgb[]{2,3,4};std::memcpy(static_cast<unsigned char*>(out)+AreaRadiance,rgb,sizeof(rgb));
    Put(out,0x7C,std::uint32_t{0xABCDEF01});
    return 0x8877665544332211ULL;
}
void Setup(std::uint32_t type=Omni) {
    records.fill(0xAB);areaRecord.fill(0xEF);source.fill(0xCD);Put(source.data(),0,type);
    requestedCount=1;invalidLast=false;argsPreserved=false;
    WorldLightingTestConfigure(base,NativeGenerator,NativeArea);
    WorldSettings s{};s.lightingEnabled=true;s.localLightBrightness=2;
    s.localLightTint[0]=0.5f;s.localLightTint[1]=0.25f;s.localLightTint[2]=0;WorldSetSettings(s);
}
int Generate(std::uintptr_t caller=base+PerViewReturn) {return WorldLightingTestGenerate(records.data(),matrixSentinel,source.data(),true,1.75f,caller);}
std::uintptr_t Area(std::uintptr_t caller=base+AreaReturnA) {return WorldLightingTestPackArea(source.data(),areaRecord.data(),13,caller);}
float AreaChannel(unsigned channel) {float value=0;std::memcpy(&value,areaRecord.data()+AreaRadiance+channel*4,4);return value;}
}
int main() {
    Setup();auto initial=source;
    Check(Generate()==1 && argsPreserved,"five native arguments and result preserved");
    Check(Near(Channel(0,0),2)&&Near(Channel(0,1),1.5f)&&Channel(0,2)==0,"native radiance tint and brightness");
    Put(initial.data(),0xD0,std::uint32_t{0xABCDEF01});Check(source==initial,"source colors untouched and native geometry retained");
    Check(records[Radiance-1]==0xAB && records[Radiance+12]==0xAB,"only RGB twelve bytes changed");
    requestedCount=6;Check(Generate()==6 && Near(Channel(5,0),7)&&Near(Channel(5,1),1.5f),"all six omni sublights modified");
    for(unsigned i=0;i<100;++i) Generate();Check(Near(Channel(5,1),1.5f),"fresh output baseline prevents accumulation");
    Setup(Spot);Generate();Check(Near(Channel(0,1),1.5f),"spot classified from native type");
    Setup(Ortho);Generate();Check(Near(Channel(0,1),1.5f),"ortho classified from native type");
    Setup(Directional);Generate();Check(Channel(0,1)==3,"directional sunlight kept independent");
    Setup(1234);Generate();Check(Channel(0,1)==3,"unknown source type forwards");
    Setup();Generate(base+PerViewReturn+1);Check(Channel(0,1)==3,"other generator caller forwards");
    auto s=WorldGetSettings();s.lightingEnabled=false;WorldSetSettings(s);Generate();
    Check(Channel(0,1)==3 && std::strstr(WorldLightingStatus(),"disabled"),"disabled controls retain native output");
    s.lightingEnabled=true;s.localLightBrightness=0;WorldSetSettings(s);Generate();Check(Channel(0,0)==0&&Channel(0,1)==0&&Channel(0,2)==0,"zero native light brightness");
    Setup();requestedCount=6;invalidLast=true;Generate();
    Check(Channel(0,1)==3&&std::isnan(Channel(5,2)),"entire RGB batch validates before changes");
    Setup();requestedCount=7;Generate();Check(Channel(0,1)==3,"unexpected result count rejects modification");
    Setup();requestedCount=0;Check(Generate()==0&&records[Radiance]==0xAB,"empty output forwards");
    Setup();Generate();WorldLightingStop("test");WorldLightingActivate();Generate();
    Check(Channel(0,1)==3 && std::strstr(WorldLightingStatus(),"resident"),"stopped hook remains native and resident");
    Setup();originalEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);originalResume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread inOriginal([]{Generate();});WaitForSingleObject(originalEntered,INFINITE);
    WorldLightingStop("native generator busy");SetEvent(originalResume);inOriginal.join();
    CloseHandle(originalEntered);CloseHandle(originalResume);originalEntered=originalResume=nullptr;
    Check(Channel(0,1)==3,"stop does not wait on original and blocks later writes");
    Setup();auto areaSource=source;auto areaNative=areaRecord;
    const float rgb[]{2,3,4};std::memcpy(areaNative.data()+AreaRadiance,rgb,sizeof(rgb));Put(areaNative.data(),0x7C,std::uint32_t{0xABCDEF01});
    Check(Area()==0x8877665544332211ULL && argsPreserved,"area pack all arguments and full RAX preserved");
    Check(Near(AreaChannel(0),2)&&Near(AreaChannel(1),1.5f)&&AreaChannel(2)==0,"area GPU radiance changes");
    Check(source==areaSource,"area scene source retained");
    std::memcpy(areaNative.data()+AreaRadiance,areaRecord.data()+AreaRadiance,12);
    Check(areaNative==areaRecord,"area modifies twelve GPU RGB bytes only");
    for(unsigned i=0;i<100;++i)Area();Check(Near(AreaChannel(1),1.5f),"area repeated views never accumulate");
    Area(base+AreaReturnB);Check(Near(AreaChannel(1),1.5f),"second native area view caller supported");
    Area(base+AreaReturnA+1);Check(AreaChannel(1)==3,"unknown area caller retained");
    s=WorldGetSettings();s.lightingEnabled=false;WorldSetSettings(s);Area();Check(AreaChannel(1)==3,"disabled area native baseline");
    s.lightingEnabled=true;s.localLightBrightness=0;WorldSetSettings(s);Area();Check(AreaChannel(0)==0&&AreaChannel(1)==0&&AreaChannel(2)==0,"zero area radiance");
    Setup();originalEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);originalResume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread inArea([]{Area();});WaitForSingleObject(originalEntered,INFINITE);
    WorldLightingStop("native area pack busy");SetEvent(originalResume);inArea.join();
    CloseHandle(originalEntered);CloseHandle(originalResume);originalEntered=originalResume=nullptr;
    Check(AreaChannel(1)==3,"stop inside native area pack prevents later RGB writes");
    Area();Check(AreaChannel(1)==3,"area forwarding retained after stop");
    std::printf("world lighting checks: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
