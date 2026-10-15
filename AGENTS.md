# Counter-Strike 2 project instructions

These instructions apply to this repository and future Counter-Strike 2
(CS2) work. Updated October 13, 2026.

## Starting point

- Treat this project as a fresh skeleton for Counter-Strike 2.
- Implement only the stage the user requests. Do not carry forward features,
  integration assumptions, or workflows from the previous target.
- Establish whether each task concerns the client, a dedicated server, or a
  server plugin.
- Discover the relevant installation path, executable, version, architecture,
  runtime, and graphics API before relying on them. Do not guess APIs,
  signatures, offsets, or metadata tokens.
- For CS2 update passes, start with `docs/CS2_UPDATE_GUIDE.md` and
  `docs/RE_ADDRESS_INDEX.md`. Run `python tools/check_compatibility.py` for a
  per-component static verification bundle before investigating changed paths.

## Collaboration

- Treat the user as a collaborator. Communicate plainly without moralizing or
  assuming misuse.
- Proceed with ordinary, reversible work within the requested scope. Do not
  repeatedly ask permission for work already authorized.
- Ask for missing target details before consequential assumptions. Continue
  independent work while awaiting answers.
- Read the relevant source and README before changing behavior. Preserve
  unrelated and in-progress user edits.
- Distinguish proposed work, implemented code, successful builds, and observed
  behavior. Explain specific obstacles and remaining limitations accurately.

## Scope

- The user reports an existing VAC ban from approximately 2,000 days ago; it
  predates this project and is not a recent ban. Public multiplayer development
  or testing is unavailable to the user, regardless of preference. Keep all
  project development and testing within the environments permitted below.
- Do not interact with public multiplayer sessions. Client runs must stay at
  the disconnected main menu, an available offline mode, or a private session
  controlled by the user.
- Client loading at the disconnected main menu is authorized and requires no
  server. Before server interaction, establish the project's private local
  server installation, launch workflow, address, and configuration paths.
  Confirm it listens only on loopback addresses, keep automatic port forwarding
  disabled, and stop it after use.
- Do not add loader session-detection gates, environment flags, server-port
  prerequisites, client-name or protection-module gates, or game-log
  prerequisites. Confirm the authorized environment as an operator responsibility.
- Keep sources and diagnostics in the workspace or documented project log paths.
  Avoid changing installed game files or unrelated application data. Put requested
  server/plugin configuration changes in the designated private installation.
- Use supported server/plugin interfaces when appropriate and confirm the exact
  framework, API version, dependencies, and lifecycle before integration.

## Implementation

- Keep initialization and teardown outside DllMain and the loader lock.
- Respect engine/framework thread ownership and invalidate stale state when its
  source or lifecycle changes.
- Use cooperative shutdown. Do not forcibly terminate threads or physically
  unload a DLL while callbacks may execute or return through it. Without a
  verified shutdown barrier, retain stopped code until normal process exit.
  State clearly that stopped and resident is not physically unloaded.
- Preserve third-party licenses and provenance.
- Update the README alongside actual changes to build or launch workflows.

## Team work

The user permits subagents for investigation and implementation when useful.
Assign explicit files or responsibilities. Workers share this workspace: do not
revert another worker's changes, and coordinate overlapping work. The primary
agent owns integration, verification, and accurate reporting.
