// stdio entry point, for running the server locally from a checkout:
//
//   node mcp/dist/stdio.js
//
// It serves whatever the checkout has -- including uncommitted edits to docs/
// or src/ -- which is the point of running it next to the code you are changing.
// Logs go to stderr; stdout is the protocol channel.

import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { loadRepo, versionLine } from "./repo.js";
import { createServer } from "./server.js";

const repo = loadRepo();
console.error(`[mcp] ${versionLine(repo.version)} · ${repo.docs.length} docs · ${repo.sources.length} source files`);

const server = createServer(repo);
await server.connect(new StdioServerTransport());
