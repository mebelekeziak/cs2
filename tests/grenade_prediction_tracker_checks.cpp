#include "grenade_prediction_tracker.h"
#include <cstdio>
#include <limits>
#include <utility>

namespace {
int checks=0,failures=0;
void Expect(bool ok,const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n",label); }
}
GrenadePath Path(GrenadeKind kind=GrenadeKind::Smoke,float start=0,bool complete=true) {
    GrenadePath path;
    path.kind=kind;
    path.nodes={{{start,0,1},0,false},{{start+10,0,0},1,true},{{start+20,0,0},2,false}};
    path.end=complete?GrenadeEnd::Settled:GrenadeEnd::Budget;
    path.finalPosition=path.nodes.back().position; path.finalAge=2;
    return path;
}
}
int main() {
    GrenadePredictionTracker tracker;
    std::uint64_t firstFrame=999;
    Expect(!tracker.FirstObservedFrame(42,firstFrame) && firstFrame==999,"missing first-observed frame leaves caller output unchanged");
    GrenadeKind foundKind=GrenadeKind::HE; bool foundEnemy=true;
    Expect(!tracker.Lookup(42,foundKind,foundEnemy) && foundKind==GrenadeKind::HE && foundEnemy,
        "missing lookup preserves caller output values");
    Expect(!tracker.Complete(42),"missing handle has no complete prediction");
    Expect(!tracker.Observe(42,GrenadeKind::Smoke,false),"observations require an explicit entity scan");
    tracker.BeginFrame();
    Expect(tracker.Observe(42,GrenadeKind::Smoke,false,12),"live local projectile is admitted");
    Expect(tracker.FirstObservedFrame(42,firstFrame) && firstFrame==12,"new full handle records its first owner frame");
    Expect(tracker.Lookup(42,foundKind,foundEnemy) && foundKind==GrenadeKind::Smoke && !foundEnemy,
        "lookup returns copied ownership classification without a retained thrower pointer");
    Expect(!tracker.Complete(42),"new observed projectile has no complete path yet");
    Expect(tracker.SetPath(42,Path()),"first complete prediction is retained");
    Expect(tracker.Complete(42),"complete query allows native simulation to stop after first complete path");
    tracker.EndFrame();
    auto capture=tracker.Capture();
    Expect(capture.size()==1 && capture[0].path.nodes.front().position.x==0,"capture contains complete copied path from original throw");
    bool persisted=true;
    for (int frame=0;frame<5000;++frame) {
        tracker.BeginFrame(); persisted &= tracker.Observe(42,GrenadeKind::Smoke,false); tracker.EndFrame();
    }
    capture=tracker.Capture();
    Expect(persisted && capture.size()==1 && capture[0].path.nodes.front().position.x==0,
        "live projectile persists without arbitrary expiry or a newly computed path");
    Expect(tracker.FirstObservedFrame(42,firstFrame) && firstFrame==12,"repeated observations do not advance original admission frame");
    tracker.BeginFrame(); tracker.Observe(42,GrenadeKind::Smoke,false);
    Expect(!tracker.SetPath(42,Path(GrenadeKind::Smoke,100)),"remaining-flight updates never shorten first complete trajectory");
    tracker.EndFrame();
    Expect(tracker.Capture()[0].path.nodes.front().position.x==0,"retained full path survives rejected replacement");

    tracker.BeginFrame(); tracker.Observe(42,GrenadeKind::Smoke,false);
    Expect(tracker.SetPath(42,Path(GrenadeKind::Smoke,-10),true),"prepared local throw may supersede ordinary observed prediction once");
    Expect(!tracker.SetPath(42,Path(GrenadeKind::Smoke,-20),true),"first complete prepared path stays immutable");
    tracker.EndFrame();
    Expect(tracker.Capture()[0].path.nodes.front().position.x==-10,"prepared prefix remains after grenade release or weapon switch");

    tracker.BeginFrame(); tracker.Observe(43,GrenadeKind::Molotov,true);
    Expect(tracker.SetPath(43,Path(GrenadeKind::Molotov)),"enemy projectile stores its first thrown path");
    Expect(!tracker.SetPath(43,Path(GrenadeKind::Molotov,-10),true),"enemy path cannot receive local prepared throw association");
    tracker.Observe(42,GrenadeKind::Smoke,false); tracker.EndFrame();
    Expect(tracker.Capture(true,false).size()==1 && !tracker.Capture(true,false)[0].enemy,"local filter hides only enemy paths");
    Expect(tracker.Capture(false,true).size()==1 && tracker.Capture(false,true)[0].enemy,"enemy filter hides only local paths");
    Expect(tracker.Capture(false,false).empty() && tracker.Size()==2,"disabled drawing filters preserve live tracker state");
    tracker.BeginFrame(); tracker.End(42);
    Expect(!tracker.FirstObservedFrame(42,firstFrame),"actual end retires original admission stamp");
    Expect(!tracker.Lookup(42,foundKind,foundEnemy) && !tracker.Complete(42),"actual end retires lookup classification and completion state");
    Expect(!tracker.Observe(42,GrenadeKind::Smoke,false) && !tracker.SetPath(42,Path()),"actual detonation cannot resurrect in the same scan");
    tracker.Observe(43,GrenadeKind::Molotov,true); tracker.EndFrame();
    Expect(tracker.Capture().size()==1 && tracker.Capture()[0].enemy,"actual smoke deployment removes its trajectory immediately");
    tracker.BeginFrame(); tracker.End(43); tracker.EndFrame();
    Expect(tracker.Size()==0 && tracker.Capture().empty(),"actual fire ignition removes enemy trajectory immediately");

    constexpr std::uint32_t oldSerial=0x10005,newSerial=0x20005;
    tracker.BeginFrame(); tracker.Observe(oldSerial,GrenadeKind::HE,true,100); tracker.SetPath(oldSerial,Path(GrenadeKind::HE)); tracker.EndFrame();
    tracker.BeginFrame(); tracker.Observe(newSerial,GrenadeKind::HE,true,101); tracker.SetPath(newSerial,Path(GrenadeKind::HE,50)); tracker.EndFrame();
    capture=tracker.Capture();
    Expect(tracker.Size()==1 && capture[0].path.nodes.front().position.x==50,"reused entity index with a new full handle serial cannot inherit old path");
    Expect(tracker.FirstObservedFrame(newSerial,firstFrame) && firstFrame==101 && !tracker.FirstObservedFrame(oldSerial,firstFrame),
        "recycled entity serial gets a fresh first-observed frame");
    tracker.BeginFrame(); tracker.EndFrame();
    Expect(tracker.Size()==0,"complete scan evicts a projectile that disappeared without an explicit effect transition");

    tracker.BeginFrame(); tracker.Observe(10,GrenadeKind::Flash,false);
    Expect(tracker.SetPath(10,Path(GrenadeKind::Flash,0,false)),"bounded incomplete prefix can be retained");
    Expect(!tracker.Complete(10),"partial prefix keeps native recomputation eligible");
    Expect(!tracker.Capture()[0].path.Terminal(),"incomplete tracker path has no final landing marker");
    Expect(!tracker.SetPath(10,Path(GrenadeKind::Flash,10,false)),"later incomplete update cannot shorten observed prefix");
    Expect(tracker.SetPath(10,Path(GrenadeKind::Flash,0,true)),"complete prediction upgrades an incomplete prefix");
    tracker.EndFrame();
    Expect(tracker.Capture()[0].path.Terminal(),"upgraded endpoint is available for the landing icon");
    tracker.BeginFrame(); tracker.Observe(10,GrenadeKind::Decoy,false);
    Expect(tracker.Capture().empty(),"kind change invalidates old path even if handle is unchanged");
    tracker.SetPath(10,Path(GrenadeKind::Decoy));
    tracker.Observe(10,GrenadeKind::Decoy,true);
    Expect(tracker.Capture().empty(),"ownership association change invalidates old local path");
    tracker.EndFrame();

    tracker.Clear(); tracker.BeginFrame(); tracker.Observe(20,GrenadeKind::Smoke,false);
    auto invalid=Path(); invalid.end=GrenadeEnd::TraceFailure;
    Expect(!tracker.SetPath(20,std::move(invalid)),"failed trace path is not retained as a landing prediction");
    invalid=Path(); invalid.nodes[1].time=-1;
    Expect(!tracker.SetPath(20,std::move(invalid)),"non-monotonic path ages reject");
    invalid=Path(); invalid.nodes[1].position.x=std::numeric_limits<float>::quiet_NaN();
    Expect(!tracker.SetPath(20,std::move(invalid)),"nonfinite path position rejects");
    invalid=Path(); invalid.finalPosition.x+=1;
    Expect(!tracker.SetPath(20,std::move(invalid)),"terminal point must match retained final path node");
    Expect(!tracker.SetPath(20,Path(GrenadeKind::HE)),"mismatched grenade type rejects");
    Expect(!tracker.SetPath(21,Path()),"unobserved handle cannot publish a path");
    invalid=Path(); invalid.nodes.resize(GrenadePredictionTracker::MaxNodes+1);
    Expect(!tracker.SetPath(20,std::move(invalid)),"oversized path rejects before retaining unbounded memory");
    Expect(!tracker.Observe(UINT32_MAX,GrenadeKind::Smoke,false),"invalid full handle rejects");
    tracker.Clear(); tracker.BeginFrame();
    bool admitted=true;
    for (std::uint32_t i=0;i<GrenadePredictionTracker::MaxEntries;++i) {
        admitted &= tracker.Observe(i,GrenadeKind::HE,false);
        admitted &= tracker.SetPath(i,Path(GrenadeKind::HE));
    }
    Expect(admitted && tracker.Size()==GrenadePredictionTracker::MaxEntries,"bounded tracker admits its documented maximum");
    Expect(!tracker.Observe(1000,GrenadeKind::HE,false) && tracker.Capture().size()==GrenadePredictionTracker::MaxEntries,
        "capacity exhaustion preserves existing live paths");
    tracker.EndFrame(); tracker.BeginFrame(); tracker.Observe(0,GrenadeKind::HE,false);
    for (std::uint32_t i=1;i<=GrenadePredictionTracker::MaxEntries+2;++i) tracker.End(i);
    Expect(!tracker.Observe(1000,GrenadeKind::HE,false) && tracker.Observe(0,GrenadeKind::HE,false),
        "bounded tombstone saturation denies new admissions and retains unrelated live observation");
    tracker.EndFrame();
    tracker.Clear();
    Expect(tracker.Size()==0 && tracker.Capture().empty(),"source transition or cooperative stop clears every retained path");

    tracker.BeginFrame();
    Expect(tracker.MarkLive(70,GrenadeKind::Flash,200),"physical identity can be marked live before native ownership is ready");
    foundKind=GrenadeKind::HE; foundEnemy=true;
    Expect(tracker.FirstObservedFrame(70,firstFrame) && firstFrame==200,
        "neutral receipt records physical discovery frame before ownership association");
    Expect(!tracker.Lookup(70,foundKind,foundEnemy) && foundKind==GrenadeKind::HE && foundEnemy,
        "neutral receipt does not invent local or enemy ownership");
    Expect(tracker.Capture().empty() && !tracker.Complete(70) && !tracker.SetPath(70,Path(GrenadeKind::Flash)),
        "neutral identity cannot publish or retain a path until ownership is associated");
    tracker.EndFrame();
    for (std::uint64_t frame=201;frame<205;++frame) {
        tracker.BeginFrame(); tracker.MarkLive(70,GrenadeKind::Flash,frame); tracker.EndFrame();
    }
    tracker.BeginFrame();
    Expect(tracker.Observe(70,GrenadeKind::Flash,false,205) && tracker.FirstObservedFrame(70,firstFrame) && firstFrame==200,
        "late native ownership preserves the earlier physical discovery frame across scans");
    Expect(tracker.Lookup(70,foundKind,foundEnemy) && foundKind==GrenadeKind::Flash && !foundEnemy &&
        tracker.SetPath(70,Path(GrenadeKind::Flash)),"associated neutral identity can now retain and publish its prediction");
    tracker.EndFrame(); tracker.BeginFrame(); tracker.MarkLive(70,GrenadeKind::Flash,206);
    Expect(tracker.Lookup(70,foundKind,foundEnemy) && tracker.Complete(70) && tracker.Capture().size()==1,
        "later neutral refresh does not erase established association or complete path");
    tracker.End(70);
    Expect(!tracker.MarkLive(70,GrenadeKind::Flash,207) && !tracker.Observe(70,GrenadeKind::Flash,false,207) &&
        !tracker.FirstObservedFrame(70,firstFrame),"actual end tombstones neutral and associated admission in the same scan");
    tracker.EndFrame(); tracker.BeginFrame();
    Expect(tracker.MarkLive(71,GrenadeKind::Smoke,208),"unassociated physical receipt can be admitted after an earlier grenade ends");
    tracker.EndFrame(); tracker.BeginFrame(); tracker.EndFrame();
    Expect(!tracker.FirstObservedFrame(71,firstFrame),"complete scan prunes absent neutral identities");
    tracker.Clear(); tracker.BeginFrame();
    bool neutralBounded=true;
    for (std::uint32_t i=0;i<GrenadePredictionTracker::MaxEntries;++i)
        neutralBounded &= tracker.MarkLive(i,GrenadeKind::HE,300);
    Expect(neutralBounded && tracker.Size()==GrenadePredictionTracker::MaxEntries && tracker.Capture().empty() &&
        !tracker.MarkLive(1000,GrenadeKind::HE,301),"unassociated physical receipts share the bounded entity capacity");
    Expect(tracker.Observe(0,GrenadeKind::HE,true,302) && tracker.FirstObservedFrame(0,firstFrame) && firstFrame==300,
        "ownership can associate an existing neutral receipt even when capacity is full");
    std::printf("Grenade prediction tracker: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
