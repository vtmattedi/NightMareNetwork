---
title: MCP setup
description: Connect an AI assistant to the NightMare Network knowledge base — the docs, the source and the examples — so it answers from the code rather than from memory.
section: mcp
order: 80
---

# NightMare MCP

NightMare MCP is a [Model Context Protocol](https://modelcontextprotocol.io)
server that hands an AI assistant the same documentation this website is
built from, plus the library's source and examples. Point Claude Code, Cursor,
VS Code or Claude Desktop at it, and questions like *"what does `MQTT_Send`
do with a leading slash?"* get answered from `src/Core/MQTT.cpp`, at the
revision that answered — not from what the model remembers about some other
MQTT library.

It is a thin retrieval layer. There is no vector database, no embeddings and
no generated text: every answer is a document, a source excerpt or an example
from the repository, with the library version and Git revision it came from.

## The endpoint

```
https://nightmarenetwork.mattediworks.com/mcp
```

Transport: **Streamable HTTP**. No authentication — everything it serves is
public. The same server also runs over stdio for local use (below).

## Connect a client

### Claude Code

```sh
claude mcp add --transport http nightmare https://nightmarenetwork.mattediworks.com/mcp
```

Then in a session: `/mcp` shows it connected, and any question about the
library will use it. To scope it to one project rather than your user config,
add `--scope project`, which writes `.mcp.json` in the repository.

### Cursor

`.cursor/mcp.json` in the project, or the global one from *Settings → MCP*:

```json
{
  "mcpServers": {
    "nightmare": {
      "url": "https://nightmarenetwork.mattediworks.com/mcp"
    }
  }
}
```

### VS Code (Copilot agent mode)

`.vscode/mcp.json`:

```json
{
  "servers": {
    "nightmare": {
      "type": "http",
      "url": "https://nightmarenetwork.mattediworks.com/mcp"
    }
  }
}
```

### Claude Desktop

Claude Desktop connects to remote servers through *Settings → Connectors →
Add custom connector* with the endpoint URL. On plans without connectors, or
to run it through the stdio bridge, `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "nightmare": {
      "command": "npx",
      "args": ["-y", "mcp-remote", "https://nightmarenetwork.mattediworks.com/mcp"]
    }
  }
}
```

### Any client, over stdio

From a checkout of the repository:

```sh
cd mcp && npm install && npm run build
node dist/stdio.js          # speaks MCP on stdin/stdout
```

Configure it as a `command` server pointing at that file. It reads
`../docs`, `../src` and `../examples` relative to itself, so it serves whatever
revision the checkout is on — including uncommitted changes, which is the
point of running it locally while editing the library.

## What it exposes

### Tools

| tool | arguments | returns |
| --- | --- | --- |
| `search_docs` | `query`, `limit?` | ranked documentation sections with their path and a snippet |
| `get_doc` | `path` | one document, whole, with its front-matter |
| `list_docs` | `section?` | every document with title, description and section |
| `search_source` | `query`, `limit?` | matching lines in `src/` with surrounding context |
| `get_source` | `path`, `from?`, `to?` | a file or line range from `src/` |
| `get_api` | `name` | the declaration(s) for a function, class or macro, from the headers, with the doc comment above it |
| `get_example` | `name?` | an example project, or the list of them |
| `get_version` | | library version from `library.json`, Git revision, and whether the tree was dirty when the server started |

### Resources

Every document is also a resource, `nightmare://docs/<path>`, so a client
that prefers to attach context rather than call tools can do that.

## What to ask it

Some prompts that get better answers with the MCP than without:

- *"Set up a PlatformIO project for a NightMare device on a C3 SuperMini."* —
  it pulls the getting-started page and the platform pin, and knows about the
  `-I` flag.
- *"What does the backend expect from the `sensors` command?"* — the
  declaration shape, from the protocols page that was written against the
  backend's parser.
- *"Show me how `ServerVariable` decides a value is stale."* — `get_source` on
  `Core/ServerVariables.cpp`, the actual `sync()` body.
- *"Which built-in commands does the resolver answer before mine?"* — the list,
  from `Xtra/NightMareComand.cpp`, so a device command name that would be
  shadowed is caught before it is written.

## Versions

The library is consumed from Git and changes often. Every `get_version` reply,
and every tool result, carries the version and revision it was generated from:

```
NightMare Network 0.1.0 · main @ 406883a
```

If the documentation and the source disagree, the source wins, and the tool
says so.

## Running your own

The website and the MCP are two containers behind one domain — `/` and
`/mcp` — deployed with the repository's `docker-compose.yml` through
StackPort. [MCP and website design](/docs/mcp-design) has the full layout and
the reasoning behind it.
