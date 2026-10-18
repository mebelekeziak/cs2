#include "shot_log_settings.h"
#include "silent_aim_settings.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
unsigned checks=0,failures=0;
void Expect(bool condition,const char* label) {
    ++checks;
    if(!condition) { ++failures;std::printf("FAIL %s\n",label); }
}
void Routing() {
    constexpr const char* shot="AIM_SHOT #12 fired at Test Player (ak47)";
    constexpr const char* hit="AIM_HIT #12 Test Player: head, 114 health damage";
    constexpr const char* status="AIM_SHOT_LOG_STATUS awaiting native fire events";
    constexpr const char* technical="AIM_DIAGNOSTICS callbacks=150 samples=256";
    constexpr const char* console="AIM_CONSOLE_UNAVAILABLE engine console getter unavailable";
    for(const bool enabled:{false,true}) for(const bool verbose:{false,true}) {
        const ShotLogSettings settings{enabled,verbose};
        for(const auto* body:{shot,hit,status})
            Expect(ShotLogConsoleMessage(body,settings)==(enabled?body:nullptr),"human shot, hit and logger status follow only Shot logs toggle");
        Expect(ShotLogConsoleMessage(technical,settings)==(verbose?technical:nullptr),"technical aim diagnostics follow only Verbose toggle");
        Expect(ShotLogConsoleMessage(console,settings)==console,"console availability status remains visible independently of optional logging");
        Expect(!ShotLogConsoleMessage("MOVEMENT callbacks=12",settings) &&
            !ShotLogConsoleMessage("[other] AIM_HIT #12",settings) && !ShotLogConsoleMessage("",settings) &&
            !ShotLogConsoleMessage(nullptr,settings),"unrelated and absent messages are not routed to aim console");
    }
    char record[]="[move_tid=1234 created_ms=987654321] AIM_SHOT #1 fired";
    char original[sizeof(record)]{};std::memcpy(original,record,sizeof(record));
    const auto* body=std::strstr(record,"AIM_SHOT");
    Expect(ShotLogConsoleMessage(record,{true,false})==body && !std::memcmp(record,original,sizeof(record)),
        "exact MovementTelemetry prefix is removed by a view into unchanged record");
    constexpr const char* verboseRecord="[move_tid=42 created_ms=100] AIM_DIAGNOSTICS callbacks=2";
    const auto* verboseBody=ShotLogConsoleMessage(verboseRecord,{false,true});
    Expect(!ShotLogConsoleMessage(verboseRecord,{true,false}) &&
        verboseBody && !std::strcmp(verboseBody,"AIM_DIAGNOSTICS callbacks=2"),
        "prefix stripping preserves independent technical routing");
    for(const auto* malformed:{
        "[move_tid=1] AIM_SHOT #1", "[move_tid= created_ms=2] AIM_SHOT #1",
        "[move_tid=x created_ms=2] AIM_SHOT #1", "[move_tid=-1 created_ms=2] AIM_SHOT #1",
        "[move_tid=1 created_ms=] AIM_SHOT #1", "[move_tid=1 created_ms=x] AIM_SHOT #1",
        "[move_tid=1 created_ms=2 extra] AIM_SHOT #1", "[move_tid=1 created_ms=2]AIM_SHOT #1",
        "[move_tid=1 created_ms=2]  AIM_SHOT #1", "[move_tid=1 created_ms=2 AIM_SHOT #1",
        " [move_tid=1 created_ms=2] AIM_SHOT #1", "[move_tid=1 created_ms=2] [move_tid=3 created_ms=4] AIM_SHOT #1",
        "AIM_SHOTGUN configuration", "AIM_HITCHANCE samples=256"}) {
        Expect(!ShotLogConsoleMessage(malformed,{true,false}),"malformed or unrelated prefix is not accepted as a human result");
    }
}
}
int main() {
    Expect(ShotLogGetSettings()==ShotLogSettings{true,false},"shot logs start enabled and verbose diagnostics start disabled");
    const SilentAimSettings aim{true,17.5f,AimHitbox::Pelvis,true,83,768};
    SilentAimSetSettings(aim);
    for(const bool enabled:{false,true}) for(const bool verbose:{false,true}) {
        const ShotLogSettings value{enabled,verbose};ShotLogSetSettings(value);
        Expect(ShotLogGetSettings()==value && SilentAimGetSettings()==aim,"logging toggles publish independently without changing aim or autofire");
    }
    ShotLogSetSettings({false,true});SilentAimSetSettings({});
    Expect(ShotLogGetSettings()==ShotLogSettings{false,true},"disabling aim retains independent logging preferences");
    Routing();ShotLogSetSettings({});SilentAimSetSettings({});
    std::printf("shot_log_settings_checks: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
