// Streamable HTTP entry point, mounted at /mcp. Stateless: every POST gets a
// fresh server + transport and they are torn down when the response ends. The
// server is read-only and public, so there is no session to protect, and
// statelessness means nginx needs no affinity and a restart loses nothing.

import { randomUUID } from "node:crypto";
import express from "express";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { loadRepo, versionLine } from "./repo.js";
import { createServer, SERVER_NAME, SERVER_VERSION } from "./server.js";

const PORT = Number(process.env.PORT ?? 3000);
const PUBLIC_URL = process.env.NM_PUBLIC_URL ?? `http://localhost:${PORT}/mcp`;

const repo = loadRepo();
console.log(`[mcp] repo ${repo.root}`);
console.log(`[mcp] ${repo.docs.length} docs · ${repo.sources.length} source files · ${repo.examples.length} examples`);
console.log(`[mcp] ${versionLine(repo.version)}`);

const app = express();
app.disable("x-powered-by");
app.use(express.json({ limit: "4mb" }));

// Browser-based MCP clients need CORS; everything here is public.
app.use((req, res, next) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type, Accept, Mcp-Session-Id, Mcp-Protocol-Version, Last-Event-ID");
  res.setHeader("Access-Control-Expose-Headers", "Mcp-Session-Id, Mcp-Protocol-Version");
  if (req.method === "OPTIONS") {
    res.sendStatus(204);
    return;
  }
  next();
});

app.get("/health", (_req, res) => {
  res.json({ ok: true, name: SERVER_NAME, server: SERVER_VERSION, ...repo.version, loadedAt: repo.loadedAt });
});

app.get("/", (_req, res) => {
  res.type("text/plain").send(
    [
      `${SERVER_NAME} ${SERVER_VERSION}`,
      versionLine(repo.version),
      "",
      `MCP endpoint (Streamable HTTP): ${PUBLIC_URL}`,
      "Docs: https://nightmare.mattediworks.com/docs/mcp",
    ].join("\n")
  );
});

app.post("/mcp", async (req, res) => {
  const server = createServer(repo);
  const transport = new StreamableHTTPServerTransport({
    sessionIdGenerator: undefined, // stateless
  });
  res.on("close", () => {
    transport.close().catch(() => {});
    server.close().catch(() => {});
  });
  try {
    await server.connect(transport);
    await transport.handleRequest(req, res, req.body);
  } catch (err) {
    console.error("[mcp] request failed:", err);
    if (!res.headersSent) {
      res.status(500).json({ jsonrpc: "2.0", error: { code: -32603, message: "Internal server error" }, id: null });
    }
  }
});

// Stateless servers have no server-initiated stream to open and no session to delete.
const methodNotAllowed = (_req: express.Request, res: express.Response) => {
  res.status(405).json({ jsonrpc: "2.0", error: { code: -32000, message: "Method not allowed" }, id: null });
};
app.get("/mcp", methodNotAllowed);
app.delete("/mcp", methodNotAllowed);

app.listen(PORT, "0.0.0.0", () => {
  console.log(`[mcp] listening on http://0.0.0.0:${PORT}/mcp  (instance ${randomUUID().slice(0, 8)})`);
});
