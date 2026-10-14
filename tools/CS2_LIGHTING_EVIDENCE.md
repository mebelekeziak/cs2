# Native local-light radiance, installed CS2 1.41.9.0

Read-only local-binary research. Module RVAs throughout. Canonical verifier:
`python tools/verify_world_lighting.py`. No process launch, injection, or game
file changes were used for research or synthetic checks.

`scenesystem.dll` SHA256:
`bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831`;
AMD64 PE timestamp `0x6AC7FF95`, image size `0x978000`.

The light creation switch `0xCFF50` reads the source light-data type DWORD.
Its jump table `0xD058C` maps 1 to the constructor path labelled `OmniLight`,
2 to `DirectionalLight`, 3 to `SpotLight`, and 4 to `OrthoLight`.

Recurring per-view light gathering `0x1A1460` calls native sublight generation
`0x4A3CE0` at `0x1A205C` (return address `0x1A2061`). Its ABI is
`int(outSubLights, outMatrices, lightData, bool, float)` on Windows x64.
The source pointer is passed in R8; its RGB radiance is a float3 at `+4`.
The fifth float is read from entry RSP+0x28 (frame RBP+0x8C0).
The native function returns 1 generated record, or 6 when the omni-light
six-face option is true. Other types return zero. Record stride is `0x12C`.

Radiance is copied/scaled from source RGB into each generated record's RGB
float3 at `+0xB4`. Explicit source and output instructions include
`0x4A3F45..0x4A3F5E`, `0x4A422E..0x4A424E`, and
`0x4A4AB7..0x4A4AD9`. Generation writes some native derived geometry back into
its input; an input clone would suppress these expected updates and is
therefore inappropriate here.

The caller transfers each generated sublight record into its private per-view
record at `+0x18`, by calling `0x8D010` at `0x1A223B`. That copier preserves
RGB `+0xB4` explicitly at `0x8D099..0x8D0AF`. The per-view record's RGB is thus
at `+0xCC`. Per-view records are copied by value, including generated data,
into native light-list storage before later GPU packing.

The GPU-packing callback is `CLightBinnerGPU` primary vtable `+0x70`, whose
thunk `0x1B14A0` jumps to `0x1B14B0`. It takes a per-view record in RDX and
GPU-record output in R8. At `0x1B15DF` it passes `record+0x18` to `0x4A65F0`
at `0x1B1674`. This helper copies the transformed light data by value through
`0x8BEB0`, including RGB `+0xB4` explicitly at `0x8BF39..0x8BF4F`. The
caller's stack temporary begins at RSP+0x50; temporary RGB `+0xB4` is therefore
at frame RBP+4/+8/+0xC. Instructions `0x1B177B..0x1B17C3` read that RGB,
multiply native light factors, and write GPU-record RGB at `+0xA0/+0xA4/+0xA8`.
This proves the modified generated radiance reaches the actual GPU record,
separately from sky shader tint and screen color grading.

The implemented hook calls the original generator on its original arguments,
then modifies only generated stack-record RGB values after validating all
colors and the count. It applies only at exact return `0x1A2061`, with native
type 1/3/4. Type 2 is retained for separate sunlight controls. No persistent
scene field is changed by this modifier, no pointers are retained between
views, and all geometry, other output fields, and native return values remain
native. Repeated views rebuild their radiance baseline without accumulation.

Stop disarms first and drains only short output writes using an SRW barrier.
Original native generation runs outside that barrier. Stopped forwarding code
remains resident; queued rendering may complete after stop. This work does not
claim baked-lightmap changes, probe ambient changes, barn/area-light tinting,
or live visual validation. The separate area extension below supersedes the
earlier barn/area limitation. The lighting controls supplement the working
visible-sky controls; lighting-sky tint does not replace native light radiance.

## Area/barn extension and paused checkpoint

The area extension is implemented in `src/world_lighting.cpp`, with constants
in `include/world_lighting_offsets.h` and synthetic cases in
`tests/world_lighting_checks.cpp`. It hooks native GPU-record packing
`0x1B0FB0` only at callers returning to `0x1B0060` or `0x1B00DB`.
Its Windows x64 arguments are RCX context, RDX output GPU record, R8d integer
light index. The native caller ignores the return; the forwarding wrapper
preserves full RAX as `uintptr_t` without assigning a semantic return type.

Both callers supply their current thread's stack GPU-record array at frame
RBP+0x160, with record stride `0x90`. Original packing reads scene light RGB
float3 at `+0x2A0` and stores output GPU RGB at `+0x70/+0x74/+0x78` at
`0x1B1313..0x1B1327`; later native debug coloring may replace it before
original return. The modifier applies shared local color/brightness after
original returns, to those twelve output bytes only. It preserves all native
arguments, other GPU fields, and scene data. Native originals remain outside
the writer barrier. Separate telemetry counters report ordinary radiance
records and area GPU records.

The area array lies inside the native per-view constant-buffer stack source
at RBP-0x30. The same builder copies that source at `0x1B0CCC` through
`0x569120` into the complete `0x7DE0`-byte upload buffer, and eventually calls
the native graphics upload vtable `+0xF8` at `0x1B0F91`. These instructions
were inspected from the installed local binary. The area source/store,
caller/prolog, and stride checks are included in `verify_world_lighting.py`;
final upload checks can be added on resume if needed.

Checkpoint at the user's request to stop and resume later: all owned files
are saved and frozen. The standalone stdlib verifier passed after adding the
area extension. Synthetic tests for both native paths are written, but the
latest area extension has **not** been built or run. Remaining work is final
integration build/test, any resulting repairs, and accurate reporting. No
live client test, injection, public session, or observed lighting result is
claimed. Existing sky files were preserved throughout this lighting work.
