# Working on NightMare Network

## Repository scope

This repository contains three distinct deliverables:

- the C++ firmware library in `src/`,
- the documentation website application in `website/`,
- the MCP service in `mcp/`.

Keep changes within the deliverable the user requested. Do not assume that a
change to one deliverable authorizes corresponding implementation changes to
the other two.

For requests about C++ or firmware-library behavior, modify only the C++
library and, when necessary, the shared documentation in `docs/`. Do not edit
`website/`, its Vite application, or `mcp/` unless the user explicitly requests
that work or explicitly asks for a cross-deliverable change.

Before modifying the library, read:

- `docs/contributing/editing-library.md`
- `docs/concepts.md`
- `docs/responsibilities.md`
- `docs/architecture.md`
- `docs/architecture/design-decisions.md`
- `docs/architecture/known-gaps.md`

`src/` is the authoritative current implementation.

`Legacy/` contains the previous NightMare architecture for historical and migration reference. It is not part of the active library and should not be used as a model for new code unless the task explicitly concerns legacy behavior.

If documentation and active source disagree, active source determines current behavior and the documentation should be corrected.

Do not introduce compatibility layers, alternate Resource models, or implementations of deferred architecture without an explicit requirement.

For subsystem-specific work, follow the documentation links in `docs/contributing/editing-library.md`.
