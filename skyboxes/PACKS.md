# Locally installed skybox pack

Installed October 25, 2026 at the user's request: **Custom Skybox Pack v1 by
LynX**, shared in the [Neverlose forum thread](https://forum.neverlose.cc/t/custom-skybox-for-cs2/586843)
and its [author-linked archive](https://drive.google.com/file/d/1TPz6vQJV85SqL_jIGPH2vxlQdGxQYpTS/view).

The local import contains **296 compiled textures and 242 compiled materials**
(538 files, 2,689,693,581 bytes) under this folder's `materials/` directory.
Assets are ignored by Git and are not redistributed with the source repository.
The original archive and extracted author README are retained under
`logs/skybox-research/lynx-pack.rar` and `logs/skybox-research/lynx-pack/`.

The author README identifies LynX and Discord `desyncer`; it does not provide
a redistribution license. Original filenames and compiled resource contents
are preserved. The application constructs its own native sky material for
selected cubemap textures; the pack's material files are retained as data.

Open **World → Custom skybox**, then search or select a **Custom:** entry.
Use **Refresh skyboxes** if importing more files while the client is running.
This local asset verification does not establish successful in-game rendering.

## Alternate night skies — October 25 follow-up

Two additional compiled single-cubemap textures are installed for comparison:

- **Solitude Night**, by Andreas Mischok / Poly Haven. [Original CC0 asset](https://polyhaven.com/a/solitude_night), [compiled texture source](https://sbox.game/polyhaven/solitude_night/version/30251).
- **Modern Buildings Night**, by Greg Zaal / Poly Haven. [Original CC0 asset](https://polyhaven.com/a/modern_buildings_night), [compiled texture source](https://sbox.game/polyhaven/modern_buildings_night/version/29952).

Only their `.vtex_c` textures are imported; s&box material shaders are not used.
Both pass the import/container and catalog single-cubemap checks. This does not
establish CS2 native loading: the user reports **Skybox unavailable** for the
previous pack while built-in swaps work. The rebuilt loader now records the
selected resource and exact failing stage instead of the former generic error.
The workspace now holds 298 custom texture candidates plus nine built-in skies.

The subsequent native capture reaches loaded/parsed/named custom materials but
exposes an ownership-order bug: our loader rejected freshly created bindings
whose native strong-reference count starts at zero. The repair acquires the
first reference after type validation, before strict draw/publication checks.
The assets above remain installed. The identified rejection is repaired in
code; the user subsequently confirms custom skyboxes now work.
