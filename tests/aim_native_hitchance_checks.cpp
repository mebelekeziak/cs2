#include "aim_native_hitchance.h"
#include "aim_spread_offsets.h"
#include "aim_spread_native_reference.h"
#include <Windows.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>

namespace {
unsigned checks = 0, failures = 0;
void Expect(bool okay, const char* label) {
    ++checks;
    if (!okay) { ++failures; std::printf("FAIL %s\n", label); }
}
constexpr float circle = 6.283185482025146484375f;
struct MockState { std::uint32_t seed = 0; unsigned draws = 0; std::array<float, 4> highs{}; };
static_assert(sizeof(MockState) <= aim_spread_offsets::RandomStateSize);
struct Mock {
    unsigned ctors = 0, draws = 0;
    int bad = 0;
    bool valid = true;
    std::array<unsigned char, 0x88> engineState{};
} mock;
void* Constructor(void* memory, int seed) {
    ++mock.ctors;
    mock.valid &= memory != mock.engineState.data() &&
        reinterpret_cast<std::uintptr_t>(memory) % 16 == 0 && seed == static_cast<int>(mock.ctors);
    const auto* bytes = static_cast<unsigned char*>(memory);
    for (unsigned i = 0; i < aim_spread_offsets::RandomStateSize; ++i) mock.valid &= bytes[i] == 0;
    if (mock.bad == 1) return nullptr;
    if (mock.bad == 2) RaiseException(0xe001c521, 0, 0, nullptr);
    const MockState state{std::bit_cast<std::uint32_t>(seed)};
    std::memcpy(memory, &state, sizeof(state));
    return memory;
}
float RandomFloat(void* memory, float low, float high) {
    ++mock.draws;
    MockState state{};
    std::memcpy(&state, memory, sizeof(state));
    mock.valid &= low == 0 && (high == 1 || high == circle) && state.draws < 4;
    if (state.draws >= 4) return std::numeric_limits<float>::quiet_NaN();
    state.highs[state.draws] = high;
    spread_replay_reference::Random random(state.seed);
    float result = 0;
    for (unsigned i = 0; i <= state.draws; ++i) result = random.Float(state.highs[i]);
    ++state.draws;
    std::memcpy(memory, &state, sizeof(state));
    if (mock.bad == 3) return std::numeric_limits<float>::quiet_NaN();
    if (mock.bad == 4) return high + 1;
    return result;
}
void Reset() { mock = {}; mock.engineState.fill(0xa7); }
AimSpreadContext Context() {
    AimSpreadContext result{};
    result.randomCtor = Constructor;
    result.randomFloat = RandomFloat;
    return result;
}
AimHitchanceInput Input(unsigned count = 256) {
    AimHitchanceInput input{};
    input.capsule = {{1000,0,-8},{1000,0,8},10};
    input.range = 8192;
    input.inaccuracy = .02f;
    input.spread = .01f;
    input.samples = count;
    return input;
}
std::array<double,2> Reference(unsigned seed, const AimHitchanceInput& input, bool pattern) {
    spread_replay_reference::Random random(seed);
    float a = random.Float(1), angleA = random.Float(circle), b = 0, angleB = 0;
    if (pattern) { angleB = random.Float(circle); b = random.Float(1); }
    else { b = random.Float(1); angleB = random.Float(circle); }
    const auto shape = [&](float radius) {
        if (!input.negev || input.recoilIndex >= 3) return radius;
        // An independent exponent describes the installed repeated-square law.
        const int squares = input.recoilIndex < 1 ? 3 : input.recoilIndex < 2 ? 2 : 1;
        for (int i = 0; i < squares; ++i) radius *= radius;
        return 1 - radius;
    };
    a = shape(a); b = shape(b);
    if (input.forceMaxRadius) a = b = 1;
    if (input.fixedSpreadAngle) angleA = angleB = circle * .25f;
    a *= input.inaccuracy; b *= input.spread;
    const float x = std::cos(angleA)*a + std::cos(angleB)*b;
    const float y = std::sin(angleA)*a + std::sin(angleB)*b;
    return {x,y};
}
void DistributionChecks(const AimSpreadContext& context, bool mocked) {
    for (bool pattern : {false,true}) for (int law = 0; law < 9; ++law) {
        auto input = Input(1024);
        if (law >= 1 && law <= 5) { input.negev = true; input.recoilIndex = static_cast<float>(law-1)*.75f; }
        input.forceMaxRadius = law == 6 || law == 8;
        input.fixedSpreadAngle = law == 7 || law == 8;
        if (mocked) Reset();
        AimSpreadPattern result{};
        const char* reason = nullptr;
        Expect(AimPrepareNativeSpreadPattern(context,input,pattern,result,&reason) && result.valid,
            "all supported spread laws prepare native pattern");
        Expect(reason && std::strcmp(reason,"native random-stream samples") == 0,
            "successful native source is explicit");
        for (unsigned i = 0; i < input.samples; ++i) {
            const auto expected = Reference(i+1,input,pattern);
            Expect(result.offsets[i] == expected,"native seeded offsets match independent draw/shaping reference");
        }
        const auto estimate = AimEstimateHitchance(input,result);
        Expect(estimate.valid && estimate.samples == input.samples,"prepared native pattern uses existing geometry estimator");
        if (mocked) {
            Expect(mock.valid && mock.ctors == input.samples && mock.draws == input.samples*4,
                "every native seed owns aligned zeroed state and exactly four draws");
            bool untouched = true;
            for (auto value : mock.engineState) untouched &= value == 0xa7;
            Expect(untouched,"statistical sampling leaves unrelated engine random state untouched");
        }
    }
}
void FailureChecks() {
    auto context = Context();
    auto input = Input();
    AimSpreadPattern result{};
    const char* reason = nullptr;
    for (int bad = 1; bad <= 4; ++bad) {
        Reset(); mock.bad = bad; result.valid = true;
        Expect(!AimPrepareNativeSpreadPattern(context,input,true,result,&reason) && !result.valid,
            "native null return, structured failure, NaN and out-of-range draw invalidate output");
        Expect(reason && std::strcmp(reason,"native random stream unavailable") == 0,
            "native draw failure is reported without hidden fallback");
    }
    for (int invalid = 0; invalid < 8; ++invalid) {
        auto bad = input;
        switch (invalid) {
        case 0: bad.inaccuracy = -1; break;
        case 1: bad.spread = 1.1f; break;
        case 2: bad.recoilIndex = -1; break;
        case 3: bad.inaccuracy = std::numeric_limits<float>::quiet_NaN(); break;
        case 4: bad.recoilIndex = std::numeric_limits<float>::infinity(); break;
        case 5: bad.spread = std::numeric_limits<float>::infinity(); break;
        case 6: bad.samples = 0; break;
        case 7: bad.samples = AimSpreadPattern::Capacity+1; break;
        }
        Reset(); result.valid = true;
        Expect(!AimPrepareNativeSpreadPattern(context,bad,true,result,&reason) && !result.valid && !mock.ctors,
            "invalid copied distribution rejects before native calls");
    }
    for (bool ctor : {false,true}) {
        auto missing = context;
        if (ctor) missing.randomCtor = nullptr; else missing.randomFloat = nullptr;
        Reset(); result.valid = true;
        Expect(!AimPrepareNativeSpreadPattern(missing,input,true,result) && !result.valid && !mock.ctors,
            "missing native method invalidates old pattern");
    }
    input.inaccuracy = input.spread = 0;
    Reset();
    Expect(AimPrepareNativeSpreadPattern(context,input,true,result) && mock.ctors == 1 && mock.draws == 4,
        "zero spread validates one native stream and retains full requested sample count");
    Expect(result.samples == input.samples && AimEstimateHitchance(input,result).percent == 100,
        "zero spread native pattern uses identical-ray geometry optimization");
    input.angles.yaw = 10;
    Expect(AimEstimateHitchance(input,result).percent == 0,"zero-spread off-target native pattern remains a miss");
    input = Input(1); Reset();
    Expect(AimPrepareNativeSpreadPattern(context,input,false,result) && mock.ctors == 1,
        "one-sample native pattern is supported");
    const auto first = result.offsets[0];
    Reset(); AimPrepareNativeSpreadPattern(context,input,true,result);
    Expect(first != result.offsets[0],"live copied pattern flag changes second draw order");
    const auto oldOffset = result.offsets[0];
    input.spread *= 2; Reset(); AimPrepareNativeSpreadPattern(context,input,true,result);
    Expect(oldOffset != result.offsets[0] && result.spread == input.spread,
        "fresh native request incorporates changed model parameters");
}
void CompareSampling(const AimSpreadContext& context) {
    for (bool pattern : {false,true}) for (unsigned samples : {256u,1024u}) {
        auto input = Input(samples);
        // A zero-length capsule has an analytic silhouette in tangent space.
        input.capsule = {{1000,0,0},{1000,0,0},10}; input.spread = 0;
        AimSpreadPattern native{};
        Expect(AimPrepareNativeSpreadPattern(context,input,pattern,native),"native comparison prepares");
        const auto seeded = AimEstimateHitchance(input,native),halton = AimEstimateHitchance(input);
        const float analytic = 1000/std::sqrt(1000.0f*1000-100)/input.inaccuracy;
        std::printf("patterns=%d samples=%u analytic=%.5f Halton=%.5f native=%.5f\n",
            pattern,samples,analytic,halton.percent,seeded.percent);
        Expect(seeded.valid && halton.valid,"both statistical ensembles evaluate same copied sphere");
    }
    for (bool pattern : {false,true}) for (int law = 0; law < 7; ++law) {
        auto input = Input(65536);
        if (law >= 1 && law <= 3) { input.negev = true; input.recoilIndex = static_cast<float>(law)-1; }
        input.forceMaxRadius = law == 4 || law == 6;
        input.fixedSpreadAngle = law == 5 || law == 6;
        const auto dense = AimEstimateHitchance(input);
        for (unsigned samples : {256u,1024u}) {
            input.samples = samples;
            AimSpreadPattern native{};
            Expect(AimPrepareNativeSpreadPattern(context,input,pattern,native),"native law comparison prepares");
            const auto seeded = AimEstimateHitchance(input,native),halton = AimEstimateHitchance(input);
            std::printf("law=%d patterns=%d samples=%u denseHalton=%.5f Halton=%.5f native=%.5f\n",
                law,pattern,samples,dense.percent,halton.percent,seeded.percent);
            Expect(dense.valid && seeded.valid && halton.valid,"all law comparison estimates remain valid");
        }
    }
}
bool NativeTier0(const wchar_t* path) {
    const auto module = LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) { std::printf("Native tier0 load failed: %lu\n",GetLastError()); return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto ctor = GetProcAddress(module,"??0?$CUniformRandomStreamImpl@VCThreadNullMutex@@@@QEAA@H@Z");
    const auto random = GetProcAddress(module,"?RandomFloat@?$CUniformRandomStreamImpl@VCThreadNullMutex@@@@QEAAMMM@Z");
    const bool matched = dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE &&
        nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt->FileHeader.TimeDateStamp == aim_spread_offsets::Tier0Timestamp &&
        nt->OptionalHeader.SizeOfImage == aim_spread_offsets::Tier0ImageSize &&
        reinterpret_cast<std::uintptr_t>(ctor) == base+aim_spread_offsets::RandomCtor &&
        reinterpret_cast<std::uintptr_t>(random) == base+aim_spread_offsets::RandomFloat &&
        !std::memcmp(reinterpret_cast<const void*>(ctor),aim_spread_offsets::RandomCtorBytes,sizeof(aim_spread_offsets::RandomCtorBytes)) &&
        !std::memcmp(reinterpret_cast<const void*>(random),aim_spread_offsets::RandomFloatBytes,sizeof(aim_spread_offsets::RandomFloatBytes));
    Expect(matched,"loaded tier0 named methods and runtime identity match frozen private RNG contract");
    if (matched) {
        AimSpreadContext context{};
        context.tier0 = base;
        context.randomCtor = reinterpret_cast<AimSpreadRandomCtor>(ctor);
        context.randomFloat = reinterpret_cast<AimSpreadRandomFloat>(random);
        DistributionChecks(context,false);
        CompareSampling(context);
    }
    // No native object or callback survives these synchronous method calls.
    FreeLibrary(module);
    return matched;
}
}

int wmain(int argc,wchar_t** argv) {
    DistributionChecks(Context(),true);
    FailureChecks();
    if (argc == 3 && std::wcscmp(argv[1],L"--native-tier0") == 0)
        Expect(NativeTier0(argv[2]),"optional isolated tier0 parity run succeeds");
    else if (argc != 1) { std::printf("Usage: aim_native_hitchance_checks [--native-tier0 absolute-path]\n"); return 2; }
    std::printf("native hitchance: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
