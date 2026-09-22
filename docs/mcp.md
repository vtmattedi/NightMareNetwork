---
title: MCP
description: Use the NightMare Network MCP server to search the same documentation, source, examples, and revision served by the project.
section: mcp
order: 10
---

# MCP

NightMare Network includes a read-only MCP server so an AI assistant can inspect the same repository material a human developer uses.

The server loads:

```text
docs/**/*.md
src/**/*.{h,hpp,cpp,c}
examples/*
library.json
Git revision metadata
```

into memory when it starts.

There is no separate documentation database and no embedding index.

## Public endpoint

The project website currently advertises the Streamable HTTP endpoint:

```text
https://nightmare.mattediworks.com/mcp
```

The HTTP server mounts MCP at:

```text
/mcp
```

and exposes a simple health endpoint at:

```text
/health
```

## Transport model

The hosted server uses MCP Streamable HTTP.

It is intentionally stateless.

Each POST creates a fresh MCP server/transport for that request and tears it down when the response ends.

The server is read-only, so it does not maintain an authenticated mutation session.

GET and DELETE on `/mcp` return method-not-allowed responses because there is no persistent server-side session stream to manage.

## Local stdio mode

A repository checkout can also run the MCP server over stdio.

After building the MCP package:

```text
node mcp/dist/stdio.js
```

The stdio server serves the current checkout, including uncommitted edits to:

```text
docs/
src/
examples/
```

This is useful while developing the library and documentation together.

## Source-of-truth rule

The server gives clients this explicit instruction:

> If documentation and source disagree, the source is right.

That rule is important during active development.

Documentation should explain the public model, but exact behavior belongs to the active `src/` revision.

## Revision stamping

Every normal tool result ends with a version stamp similar to:

```text
NightMare Network 0.2.0 · <branch> @ <revision>
```

The values come from:

```text
library.json version
Git branch
Git revision
working-tree dirty state
```

When running from a built image without `.git`, build-time `REVISION` and `BRANCH` files can supply the revision instead.

## Repository load time

The repository is read once when the MCP process starts.

`get_version` also reports:

```text
loaded time
document count
source-file count
example count
```

If files change underneath a long-running MCP process, restart that process to reload the corpus.

## Documentation tools

### `search_docs`

Use for:

```text
concepts
protocol behavior
architecture
how-to questions
known gaps
```

Inputs:

```text
query
limit    optional, 1..25, default 8
```

Results are ranked documentation sections containing:

```text
document title
heading
document path / anchor
excerpt
revision stamp
```

The server's own recommended workflow is:

```text
search_docs
    -> get_doc
```

when the full page is needed.

### `get_doc`

Returns one documentation page in full.

Paths are relative to `docs/` and omit `.md`.

Examples:

```text
overview
getting-started
modules/resources
protocols/mqttp
architecture/known-gaps
```

Accepted input is normalized, so forms beginning with `docs/` or ending in `.md` are also stripped to the internal document path.

### `list_docs`

Lists every documentation page with:

```text
path
title
description
section
```

Optional section filters are:

```text
overview
getting-started
protocols
architecture
modules
mcp
```

The page metadata comes from each Markdown file's front matter.

## Source tools

### `search_source`

Case-insensitive search over active:

```text
src/
```

C/C++ source and header files.

Inputs:

```text
query
limit    optional, 1..50, default 20
```

Results include matching source lines with local context.

Headers are preferred in result ordering where appropriate.

Use this when the documentation does not answer an implementation question.

### `get_source`

Returns one active source file or a requested line range.

Example path:

```text
src/Core/Scheduler.h
```

The tool automatically treats paths as relative to repository root / `src/`.

For large files, prefer a line range once the relevant location is known.

### `get_api`

Looks for a declaration by exact identifier.

It recognizes public declarations such as:

```text
function
method
class
struct
enum
macro
extern
```

and returns the declaration together with its nearby documentation comment when available.

Examples:

```text
ManagedState
gResourcesManager
MQTT_Publish
SchedulerRunMode
NM_ENABLE_TELEMETRY
```

When an exact declaration is not found, the tool returns nearby source mentions.

For a known symbol, prefer `get_api` over a broad source search.

## Example tool

### `get_example`

With no `name`, lists directories under:

```text
examples/
```

With a name, returns:

```text
README
platformio.ini
main.cpp
configuration headers
other text files in the example
```

Binary/image/font files are excluded from the loaded example corpus.

## Example authority

The public examples are kept against the active Resource API and are loaded directly from `examples/`.

Examples are still illustrative rather than normative. When an exact declaration or edge-case behavior matters, use `get_api` / `get_source` and apply the MCP rule:

> source wins when repository material disagrees.

## Version tool

### `get_version`

Returns:

```text
library
branch
revision
dirty
revision source
loaded time
document count
source-file count
example count
```

It also reminds Git consumers that the repository can be tracked directly:

```ini
lib_deps =
    https://github.com/vtmattedi/NightMareNetwork.git
```

and that a Git tag can be appended to pin a release.

## MCP resources

In addition to tools, the server exposes MCP resources.

### Documentation resources

URI template:

```text
nightmare://docs/{path}
```

Each document is available as Markdown.

### Version resource

URI:

```text
nightmare://version
```

returns the concise library/revision stamp.

## Recommended assistant workflow

For a conceptual question:

```text
search_docs
get_doc
```

For an exact API symbol:

```text
get_api
```

For a behavior not explained by docs:

```text
search_source
get_source
```

For version-sensitive advice:

```text
get_version
```

For an example:

```text
get_example
```

followed by source verification when exact implementation behavior matters.

## Documentation metadata

Both the website and MCP parse the same simple front matter:

```md
---
title: Scheduler
description: Schedule command and callback work using wall or monotonic time.
section: modules
order: 30
---
```

The MCP uses:

```text
title
description
section
order
```

to index and list documents.

The website consumes the same corpus.

This is intentional: human-facing docs and AI-facing docs should not diverge into separate sources.

## Search implementation

The current MCP uses an in-memory full-text search rather than embeddings.

The repository is small enough that a local linear scan avoids:

```text
external search infrastructure
vector database
network dependency
index synchronization service
```

This keeps the MCP easy to deploy and ensures its answers are tied directly to one loaded repository revision.

## Read-only boundary

The current MCP server is a repository information surface.

It does not edit source, create branches, or push commits.

Its purpose is to make the project's own docs and source inspectable with revision context, not to act as a Git write agent.
