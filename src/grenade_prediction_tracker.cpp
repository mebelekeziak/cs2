#include "grenade_prediction_tracker.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
bool Kind(GrenadeKind kind) noexcept {
    return static_cast<unsigned>(kind)<=static_cast<unsigned>(GrenadeKind::Incendiary);
}
bool Position(EspVec3 p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x)<1000000 && std::abs(p.y)<1000000 && std::abs(p.z)<1000000;
}
bool Valid(const GrenadePath& path) noexcept {
    if (!Kind(path.kind) || path.nodes.empty() || path.nodes.size()>GrenadePredictionTracker::MaxNodes ||
        (!path.Terminal() && path.end!=GrenadeEnd::Budget) || !Position(path.finalPosition) ||
        !std::isfinite(path.finalAge) || path.finalAge<0 || path.finalAge>=1000000) return false;
    float previous=0;
    for (const auto& node:path.nodes) {
        if (!Position(node.position) || !std::isfinite(node.time) || node.time<previous || node.time>=1000000)
            return false;
        previous=node.time;
    }
    if (path.finalAge+0.0001f<previous) return false;
    const auto& last=path.nodes.back().position;
    return std::abs(last.x-path.finalPosition.x)<0.001f &&
        std::abs(last.y-path.finalPosition.y)<0.001f && std::abs(last.z-path.finalPosition.z)<0.001f;
}
}

void GrenadePredictionTracker::BeginFrame() noexcept {
    // An interrupted scan does not evict anything. A subsequent complete scan
    // must explicitly observe each surviving entity before EndFrame.
    for (auto& entry:entries_) entry.seen=false;
    endedCount_=0; saturated_=false; frameOpen_=true;
}
bool GrenadePredictionTracker::MarkLive(std::uint32_t handle,GrenadeKind kind,std::uint64_t firstFrame) {
    if (!frameOpen_ || handle==UINT32_MAX || !Kind(kind) ||
        std::find(ended_.begin(),ended_.begin()+endedCount_,handle)!=ended_.begin()+endedCount_) return false;
    auto entry=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& value) { return value.handle==handle; });
    if (entry!=entries_.end()) {
        if (entry->kind!=kind) {
            const auto originalFrame=entry->firstFrame;
            *entry=Entry{}; entry->handle=handle; entry->kind=kind;
            entry->firstFrame=originalFrame;
        }
        entry->seen=true; return true;
    }
    if (saturated_ || entries_.size()>=MaxEntries) return false;
    Entry next;
    next.handle=handle; next.firstFrame=firstFrame; next.kind=kind; next.seen=true;
    entries_.push_back(std::move(next)); return true;
}
bool GrenadePredictionTracker::Observe(std::uint32_t handle,GrenadeKind kind,bool enemy,std::uint64_t firstFrame) {
    if (!MarkLive(handle,kind,firstFrame)) return false;
    auto entry=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& value) { return value.handle==handle; });
    if (entry->associated && entry->enemy!=enemy) {
        entry->path={}; entry->hasPath=false; entry->prepared=false;
    }
    entry->associated=true; entry->enemy=enemy;
    return true;
}
bool GrenadePredictionTracker::SetPath(std::uint32_t handle,GrenadePath&& path,bool preferPrepared) {
    if (!frameOpen_ || !Valid(path)) return false;
    auto entry=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& value) { return value.handle==handle; });
    if (entry==entries_.end() || !entry->associated || !entry->seen || path.kind!=entry->kind || (preferPrepared && entry->enemy)) return false;
    if (entry->hasPath) {
        if (entry->path.Terminal()) {
            if (!preferPrepared || entry->prepared || !path.Terminal()) return false;
        } else if (!path.Terminal()) {
            // Keep the first full observed prefix instead of shortening it on
            // every frame while native tracing remains budget-limited.
            return false;
        }
    }
    if (path.nodes.capacity()>MaxNodes) {
        std::vector<GrenadePathNode> bounded(path.nodes.begin(),path.nodes.end());
        path.nodes.swap(bounded);
    }
    entry->path=std::move(path); entry->hasPath=true; entry->prepared=preferPrepared;
    return true;
}
void GrenadePredictionTracker::End(std::uint32_t handle) noexcept {
    entries_.erase(std::remove_if(entries_.begin(),entries_.end(),
        [&](const Entry& entry) { return entry.handle==handle; }),entries_.end());
    if (!frameOpen_ || handle==UINT32_MAX ||
        std::find(ended_.begin(),ended_.begin()+endedCount_,handle)!=ended_.begin()+endedCount_) return;
    if (endedCount_<MaxEntries) ended_[endedCount_++]=handle;
    else saturated_=true; // Bounded tombstones: deny further new admissions.
}
void GrenadePredictionTracker::EndFrame() noexcept {
    if (!frameOpen_) return;
    entries_.erase(std::remove_if(entries_.begin(),entries_.end(),
        [](const Entry& entry) { return !entry.seen; }),entries_.end());
    frameOpen_=false;
}
std::vector<GrenadePredictionVisual> GrenadePredictionTracker::Capture(bool local,bool enemies) const {
    std::vector<GrenadePredictionVisual> result;
    result.reserve(entries_.size());
    for (const auto& entry:entries_)
        if (entry.associated && entry.hasPath && (entry.enemy?enemies:local)) result.push_back({entry.path,entry.enemy});
    return result;
}
bool GrenadePredictionTracker::Lookup(std::uint32_t handle,GrenadeKind& kind,bool& enemy) const noexcept {
    const auto entry=std::find_if(entries_.begin(),entries_.end(),
        [&](const Entry& value) { return value.handle==handle; });
    if (entry==entries_.end() || !entry->associated) return false;
    kind=entry->kind; enemy=entry->enemy; return true;
}
bool GrenadePredictionTracker::Complete(std::uint32_t handle) const noexcept {
    const auto entry=std::find_if(entries_.begin(),entries_.end(),
        [&](const Entry& value) { return value.handle==handle; });
    return entry!=entries_.end() && entry->associated && entry->hasPath && entry->path.Terminal();
}
bool GrenadePredictionTracker::FirstObservedFrame(std::uint32_t handle,std::uint64_t& frame) const noexcept {
    const auto entry=std::find_if(entries_.begin(),entries_.end(),
        [&](const Entry& value) { return value.handle==handle; });
    if (entry==entries_.end()) return false;
    frame=entry->firstFrame; return true;
}
void GrenadePredictionTracker::Clear() noexcept {
    entries_.clear(); endedCount_=0; frameOpen_=false; saturated_=false;
}
std::size_t GrenadePredictionTracker::Size() const noexcept { return entries_.size(); }
