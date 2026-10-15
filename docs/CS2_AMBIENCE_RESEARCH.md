# Native ambience follow-up — October 15, 2026

The requested comparison began with Neverlose's own CS2 release notes, rather
than third-party offset lists or its older CS:GO documentation:

- [April 2024 ambience update](https://forum.neverlose.cc/t/neverlose-cs2-april-update/445771):
  lighting, skybox changes, sun/cloud modulation and world modulation.
- [February 2026 update](https://forum.neverlose.cc/t/major-cs2-product-and-security-update-04-02-2026/587437):
  fog height, bloom and postprocessing controls.
- [September 2026 update](https://forum.neverlose.cc/t/major-cs2-product-update-double-precision-15-09-26/592958):
  color correction, exposure modes and skybox fog.

Those notes identify feature categories, not their implementation or native
shader contracts. They do not establish that another product rebakes maps or
that our controls reproduce its output. The user clarified that this request
concerns actual ambient/local lighting, and excluded additional tone grading.

| Relevant category | Existing project behavior | Follow-up |
| --- | --- | --- |
| Direct sunlight | Native per-view sunlight radiance tint/intensity | Retained separately from ambient fill |
| Ambient illumination | Baked map/probe illumination retained; no diffuse-fill control | Added native additive diffuse fill with independent color/intensity |
| Local lighting | Generated point/spot RGB and a reflection-volume multiplier incorrectly identified as area-light color | Replace those tint hooks with the actual local-light GPU-record path consumed by world shaders |
| World modulation | Flat static-world material option; arbitrary albedo tint unimplemented | No speculative material/texture mutation |
| Skybox / sun / clouds | Existing sky draw tint and probe/reflection tint | Retained; sky appearance alone does not change baked map lighting |
| Color correction, fog, weather | Existing screen grading; no new controls in this pass | Outside the clarified ambient/local-light fix |

The user's screenshot and report motivate the investigation. CPU callback
counts alone cannot prove a visible lighting change. Native ambient fill adds
diffuse radiance before surface coloring and fog; it does not subtract or
recolor existing baked irradiance, introduce shadow geometry, or rebake indirect
bounces. Keep that distinction when evaluating the next offline observation.

Exact installed-binary/shader evidence and lifecycle constraints are in
[baked/environment lighting evidence](../tools/CS2_BAKED_LIGHT_EVIDENCE.md) and
[local-light evidence](../tools/CS2_LIGHTING_EVIDENCE.md).
