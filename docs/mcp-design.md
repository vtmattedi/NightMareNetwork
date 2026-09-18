---
title: MCP and website design
description: The design notes for the NightMare MCP, the website, and how they are deployed alongside the library.
section: mcp
order: 90
---


# NightMare MCP

## Overview

**NightMare MCP** is the documentation and AI interface for the **NightMare Network** library.

NightMare Network is an ESP32/ESP-IDF/Arduino-oriented C/C++ networking and device library. The library is actively developed and may receive frequent incremental changes.

The MCP exists to make the current library knowledge, documentation, examples, and relevant source information available to MCP-compatible AI assistants.

The public web interface and MCP should be hosted under a single domain:

```text
https://nightmarenetwork.mattediworks.com/
```

with:

```text
/       → NightMare Network website
/mcp    → MCP endpoint
```

---

# Goals

## 1. Keep the library and its knowledge coupled

The NightMare Network library and its documentation should remain in the same Git repository.

Changes to the library should be easy to document in the same change.

Example:

```text
src/Core/MQTT.cpp
src/Core/MQTT.h
docs/core/mqtt.md
```

A feature change should be able to update both implementation and documentation in the same commit.

The goal is to avoid maintaining a separate documentation project that can become outdated relative to the library.

---

## 2. Keep PlatformIO consumption simple

NightMare Network is currently consumed directly from Git:

```ini
lib_deps =
    https://github.com/vtmattedi/NightMareNetwork.git
```

This workflow should remain supported.

The repository may contain documentation, website, MCP, examples, and other development files, but the C/C++ library remains centered around:

```text
src/
library.json
```

Additional repository content must not become part of the library's C++ build.

The repository is therefore larger than the PlatformIO library itself.

---

## 3. Provide a public web interface

The project should provide a public human-readable website containing:

* Project overview
* Installation instructions
* Getting started information
* Architecture documentation
* API documentation
* Module documentation
* Usage examples
* Configuration information
* Troubleshooting
* Version information
* Information about the NightMare MCP

The website should be a lightweight static application.

The initial implementation should use:

```text
React
Vite
Vite SSG
```

rather than a full documentation framework such as VitePress.

The initial website is intended to be a **single-page application generated as static files**, with client-side navigation/components where useful.

The website should consume the project's Markdown/documentation sources where practical.

---

## 4. Provide an MCP interface

The MCP should allow AI assistants to retrieve useful information about NightMare Network.

The initial MCP should remain small.

Potential capabilities include:

```text
search_docs(query)
get_doc(path)
search_source(query)
get_api(name)
get_example(name)
get_version()
```

The exact tool set may change as the project develops.

The MCP should prioritize useful retrieval over unnecessary complexity.

---

## 5. Use the same knowledge for humans and AI

The website and MCP should consume the same underlying documentation whenever practical.

Conceptually:

```text
                 NightMare Network repository
                           │
                ┌──────────┴──────────┐
                │                     │
                ▼                     ▼
             Markdown              Source
                │                     │
          ┌─────┴─────┐               │
          ▼           ▼               │
       Website       MCP ◄────────────┘
          │           │
          ▼           ▼
        Human        AI
```

The goal is to avoid having one explanation for humans and a different manually maintained explanation for AI.

---

## 6. Be deployable through StackPort

The complete NightMare Network web/MCP environment must be deployable through **StackPort**.

Deployment should be treated as part of the project requirements, not as an external manual setup.

The repository should therefore contain the StackPort deployment definition, including:

```text
docker-compose.stackport.yml
```

and any required supporting configuration.

The deployment should expose only the required public HTTP/HTTPS interface.

Conceptually:

```text
Internet
   │
   ▼
nightmarenetwork.mattediworks.com
   │
   ▼
StackPort / Nginx
   │
   ├── /       → NightMare Network website
   │
   └── /mcp   → MCP server
```

The website and MCP services should communicate internally through the Docker network rather than exposing unnecessary container ports externally.

The deployment should be compatible with the normal StackPort application lifecycle, including:

* Initial deployment
* Update
* Restart
* Rebuild
* Configuration through environment variables where appropriate
* Health checks
* Persistent configuration only where required

The project should not require manual modification of the StackPort host to function.

---

# Definitions

## NightMare Network

The actual C/C++ library.

Current source structure:

```text
src/
├── Core/
├── HTTP/
├── Services/
├── TCP/
└── Xtra/
```

The library contains networking, services, MQTT, HTTP, TCP, OTA, timing, utilities, and related functionality.

---

## NightMare MCP

The MCP server exposing NightMare Network knowledge to AI assistants.

The MCP is **not part of the C++ library**.

It is an auxiliary service that consumes the library's documentation and, where appropriate, source code and examples.

---

## NightMare Website

The public human-facing web interface for NightMare Network.

Current target:

```text
https://nightmarenetwork.mattediworks.com/
```

The website should initially be implemented using:

```text
React
Vite
Vite SSG
```

The generated output should be static and suitable for serving from a lightweight web server/container.

---

## MCP Endpoint

The public MCP endpoint:

```text
https://nightmarenetwork.mattediworks.com/mcp
```

The MCP endpoint should be proxied by the web server to the MCP application.

The MCP application itself does not need to expose a public network port.

Conceptually:

```text
Internet
   │
   ▼
Reverse Proxy
   │
   ├── /      → website
   │
   └── /mcp   → MCP server
```

---

## Documentation Source

Markdown and other source material maintained in the NightMare Network repository.

Example:

```text
docs/
├── getting-started.md
├── architecture.md
├── core/
│   ├── wifi.md
│   ├── mqtt.md
│   ├── ota.md
│   └── time.md
├── tcp/
├── services/
└── api/
```

Documentation source is considered part of the project's knowledge base.

---

## Library Source

The actual C/C++ implementation:

```text
src/
```

The MCP may inspect library source when documentation alone is insufficient.

Source inspection should not replace proper documentation.

---

## Examples

Runnable or illustrative examples demonstrating actual NightMare Network usage.

Examples are valuable MCP sources because they show how APIs are intended to be used.

Potential structure:

```text
examples/
├── basic/
├── wifi/
├── mqtt/
├── tcp/
└── services/
```

---

# Repository Model

The canonical repository may contain all NightMare Network components:

```text
NightMareNetwork/
├── src/                       ← C/C++ library
├── examples/                  ← usage examples
├── docs/                      ← documentation source
├── website/                   ← React + Vite SSG website
├── mcp/                       ← MCP implementation
│
├── docker-compose.stackport.yml
├── library.json
├── README.md
└── LICENSE
```

The repository is the canonical source of truth.

The presence of `website/` or `mcp/` does not mean those components are part of the C++ library.

---

# Deployment Model

The deployed application consists of two logical services:

```text
┌─────────────────────────────────────────────┐
│ StackPort                                   │
│                                             │
│  ┌─────────────────┐    ┌────────────────┐ │
│  │ Website         │    │ MCP            │ │
│  │ React/Vite SSG  │    │ MCP Server     │ │
│  │                 │    │                │ │
│  │ static files    │    │ /mcp           │ │
│  └────────┬────────┘    └───────┬────────┘ │
│           │                     │          │
│           └──────────┬──────────┘          │
│                      │                     │
│                 internal network           │
└──────────────────────┼─────────────────────┘
                       │
                       ▼
                 Reverse Proxy
                       │
                       ▼
     nightmarenetwork.mattediworks.com
```

The exact container layout may change, but the externally visible interface should remain:

```text
/
 /mcp
```

---

# Versioning

NightMare Network is actively developed.

The current Git development state may change frequently.

The MCP and website should therefore distinguish between:

```text
latest development version
```

and:

```text
released version
```

When possible, MCP responses should identify the library version or Git revision from which the information was generated.

For example:

```text
NightMare Network version: 1.2.0
Git revision: abc1234
```

This is important because an API may change between releases.

---

# Development vs Release

Development projects may continue using:

```ini
lib_deps =
    https://github.com/vtmattedi/NightMareNetwork.git
```

This intentionally tracks the Git repository.

Stable consumers may eventually use a tagged version:

```ini
lib_deps =
    https://github.com/vtmattedi/NightMareNetwork.git#v1.2.0
```

The MCP should eventually be capable of representing the relevant version when answering version-specific questions.

---

# Initial MCP Scope

The first implementation should avoid unnecessary infrastructure.

Initial priorities:

1. Documentation search
2. Documentation retrieval
3. API lookup
4. Example lookup
5. Source-code search
6. Version/revision information

The initial implementation does **not** require:

* A database
* Vector database
* Embeddings
* Complex authentication
* User accounts
* Write access to the repository
* AI-generated documentation

These may be considered later if the project's size makes them useful.

---

# Source of Truth

The following hierarchy should generally be used:

```text
Actual source code
       │
       ▼
Documented public API
       │
       ▼
Examples and guides
       │
       ▼
AI-generated explanations
```

AI-generated information should never silently override the actual library implementation.

If documentation and implementation disagree, the MCP should prefer the actual source and indicate the discrepancy when relevant.

---

# Design Principle

NightMare MCP should be a **thin knowledge interface around NightMare Network**, not a second implementation of the library.

The primary project remains:

```text
NightMare Network
```

The website and MCP exist to make that project easier to understand and use.

The intended relationship is:

```text
                 NightMare Network
                  canonical project
                         │
          ┌──────────────┼──────────────┐
          │              │              │
          ▼              ▼              ▼
      PlatformIO       Website          MCP
          │              │              │
          ▼              ▼              ▼
        ESP32          Humans            AI
```

The library, documentation, examples, website, and MCP should evolve together while remaining technically separable.

The deployment must remain reproducible through StackPort using the project's Docker/Compose configuration.
