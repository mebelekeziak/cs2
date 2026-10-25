#pragma once
#include "aim_hitchance.h"
#include <cstdint>

// Offline captured client 2000935 / Steam 26144732 records, audited October 24, 2026.
// Five corrected Deagle records with unambiguous assigned impact geometry:
// every such Deagle record from the two indicated captures (three misses/two hits).
// The latest capture adds its sole unambiguous corrected Glock miss.
// Other pistol events overlap and are deliberately excluded from trajectory claims.
// Zero historical/current punch is directly logged in each selected publication.
// Revision-4 accuracy/spread uses the precise publication copy. Earlier Deagle hits
// use seven-decimal shot-detail accuracy/spread because publication fields were absent.
// Desired is the saved capsule midpoint. Observed uses saved eye and furthest impact.
// Impact geometry is a receipt, not an authoritative seed, firing origin or shot tick.
struct PistolReplaySource { const char* name; const char* sha256; };
inline constexpr PistolReplaySource PistolReplaySources[]{
    {"overlay-37892-134373047916445176.log","98fc89e1da6740dfaf06277c3d2e452e96f07b61419907c2175be31387954d18"},
    {"overlay-3024-134373028959256720.log","f74a430ac446db207898cc7fc587261d1833d25ca4d64f35e1e4fc1cbe873773"},
    {"overlay-44672-134373178430871244.log","278d98fbbad05363fa2d0404ebfc1cb13172a37f3be8eb1fc6b1b604f3370879"},
};
struct PistolSpreadReplay {
    int source, id, sequence, spreadLine, detailLine, outcome;
    const char* weapon;
    int tick, rawTick, nowTick, controllerTick;
    float rawFraction, nowFraction, inaccuracy, spread;
    bool preciseAccuracy, pattern;
    std::uint32_t oldSeed;
    AimAngles oldCommand, oldPredicted, historicalPunch, currentPunch, desired, observed;
    AimVector eye, impact; AimCapsule capsule;
};
inline constexpr PistolSpreadReplay PistolSpreadReplays[]{
    {0,1,764,691,711,1,"deagle",94236,94238,94236,94240,0.285365f,0.0f,0.590437651f,0.00200000009f,true,true,0xf7561d63u,{5.000100136f,88.56208801f,0.0f},{2.996034384f,90.70566559f,0.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},{2.95655736f,90.70559293f,0.0f},{-0.4592851387f,95.52088825f,0.0f},{-389.2145f,-480.9873f,100.9375f},{-511.8461f,787.7422f,111.1553f},{{-423.3527f,2338.0098f,-46.9802f},{-424.5184f,2338.5898f,-42.3852f},4.3f}},
    {0,2,907,806,851,1,"deagle",94380,94383,94380,94384,0.977393f,0.0f,0.598355532f,0.00200000009f,true,true,0x4d8a9585u,{13.49989986f,81.14315796f,0.0f},{2.664197206f,89.14125824f,0.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},{2.726376847f,89.14166237f,0.0f},{2.406892948f,100.2126029f,0.0f},{-444.8209f,-503.53f,89.5469f},{-512.0f,-130.6349f,73.6207f},{{-401.747f,2331.9629f,-47.8043f},{-402.9235f,2332.5596f,-43.2141f},4.3f}},
    {0,4,2034,1539,1548,1,"deagle",95510,95512,95510,95512,0.826653f,0.0f,0.0548132509f,0.00200000009f,true,true,0x33788600u,{-0.263570279f,52.3921051f,0.0f},{-0.073591501f,53.53529358f,0.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},{-0.07359004539f,53.53529286f,0.0f},{-1.074441528f,49.37538968f,0.0f},{1021.1418f,902.3299f,63.6421f},{1616.0033f,1595.7637f,80.7769f},{{1781.3223f,1930.6699f,62.9255f},{1781.4048f,1931.4006f,67.6445f},4.3f}},
    {1,3,3024,1285,1290,0,"deagle",3114,3115,3114,3115,0.197254f,0.0f,0.0042034f,0.002f,false,true,0x45255710u,{8.251267f,93.161598f,0.0f},{8.026847f,93.221939f,0.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},{8.026844596f,93.22194285f,0.0f},{8.092553426f,92.95643938f,0.0f},{-400.8238f,496.2603f,59.0248f},{-467.6365f,1789.9407f,-125.1668f},{{-447.0519f,1320.7094f,-59.8248f},{-447.4571f,1321.4236f,-55.12f},4.3f}},
    {1,4,3145,1357,1362,0,"deagle",3235,3235,3235,3236,0.132122f,0.0f,0.5977241f,0.002f,false,true,0xc6f0a376u,{-1.785625f,114.000099f,0.0f},{8.638042f,91.914581f,0.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},{8.63804251f,91.89533256f,0.0f},{8.510870372f,92.03315867f,0.0f},{-396.9093f,744.3587f,81.4849f},{-446.5393f,2142.3784f,-127.8535f},{{-427.7885f,1682.3534f,-63.4976f},{-428.1322f,1683.0216f,-58.7811f},4.3f}},
    {2,16,1848,1291,1337,1,"glock",1954,1955,1954,1955,0.198272f,0.0f,0.203515917f,0.00200000009f,true,true,0xb9b13694u,{-9.753044128f,-86.73488617f,0.0f},{-4.073308945f,-92.91712952f,0.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},{-4.073306067f,-92.9171283f,0.0f},{-11.78107221f,-89.75656261f,0.0f},{-403.8409f,1220.5728f,-12.6941f},{-399.4425f,185.3652f,203.2171f},{{-457.6205f,160.9691f,60.4873f},{-458.0563f,160.8627f,65.2422f},4.3f}},
};
// The sole usable Glock miss has an earlier successful correction on the same
// sequence/history entry, before a subsequent copy includes the shot penalty.
// This supplies measured alternative accuracy for replay; it does not identify
// the actual server firing state or prove a publication was transmitted.
struct PistolEarlierPublication {
    int shotIndex, spreadLine, hitchanceLine, clip; float recoilIndex, inaccuracy;
    std::uint32_t seed; AimAngles command, predicted;
};
inline constexpr PistolEarlierPublication PistolEarlierPublications[]{
    {5,1288,1289,8,0.0f,0.146721661f,0xd8dbe3e9u,
        {-2.521699667f,-93.500099182f,0.0f},{-4.000048637f,-93.072143555f,0.0f}},
};
