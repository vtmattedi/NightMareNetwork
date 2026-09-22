// The MCP server: tools and resources over the loaded repository. Transport
// agnostic -- http.ts and stdio.ts each build one of these and connect it.

import { McpServer, ResourceTemplate } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { type Repo, versionLine } from "./repo.js";
import { findApi, searchDocs, searchSource } from "./search.js";

export const SERVER_NAME = "nightmare-network";
export const SERVER_VERSION = "0.1.0";

const text = (t: string) => ({ content: [{ type: "text" as const, text: t }] });
const fence = (lang: string, body: string) => "```" + lang + "\n" + body + "\n```";

function langFor(path: string): string {
  if (/\.(h|hpp|cpp|c)$/.test(path)) return "cpp";
  if (path.endsWith(".ini")) return "ini";
  if (path.endsWith(".md")) return "markdown";
  if (path.endsWith(".json")) return "json";
  return "";
}

export function createServer(repo: Repo): McpServer {
  const server = new McpServer(
    { name: SERVER_NAME, version: SERVER_VERSION },
    {
      instructions: [
        "NightMare Network is an ESP32 C++ framework and MQTT resource protocol. Use search_docs",
        "first for concepts, protocols and how-tos. Before modifying the library, read",
        "contributing/editing-library plus the linked architecture documents;",
        "get_api for a specific function, class or macro; search_source / get_source when the docs",
        "are not enough. If documentation and source disagree, the source is right. Every result",
        `carries the version it came from: ${versionLine(repo.version)}.`,
      ].join(" "),
    }
  );

  const stamp = () => `\n\n— ${versionLine(repo.version)}`;

  // ------------------------------------------------------------ documentation

  server.registerTool(
    "search_docs",
    {
      title: "Search documentation",
      description:
        "Full-text search over the NightMare Network documentation. Returns ranked sections with " +
        "their document path and an excerpt. Follow up with get_doc for the whole document.",
      inputSchema: {
        query: z.string().min(1).describe("Words or a phrase, e.g. 'retained status' or 'MQTTP chunking'"),
        limit: z.number().int().min(1).max(25).optional().describe("Max results, default 8"),
      },
    },
    async ({ query, limit }) => {
      const hits = searchDocs(repo.docs, query, limit ?? 8);
      if (!hits.length) return text(`No documentation matched "${query}".${stamp()}`);
      const out = hits.map((h, i) => {
        const where = h.section.anchor ? `${h.doc.path}#${h.section.anchor}` : h.doc.path;
        return `${i + 1}. **${h.doc.title}** › ${h.section.heading}\n   path: ${where}\n   ${h.snippet}`;
      });
      return text(out.join("\n\n") + stamp());
    }
  );

  server.registerTool(
    "get_doc",
    {
      title: "Get a document",
      description:
        "Returns one documentation page in full, as markdown. Paths are relative to docs/ without " +
        "the extension, e.g. 'protocols/mqttp' or 'getting-started'. Use list_docs to see them.",
      inputSchema: { path: z.string().min(1).describe("Document path, e.g. 'protocols/resources'") },
    },
    async ({ path }) => {
      const p = path.replace(/^\/?docs\//, "").replace(/\.md$/, "").replace(/^\/+|\/+$/g, "");
      const doc = repo.docs.find((d) => d.path === p) ?? repo.docs.find((d) => d.path.endsWith("/" + p));
      if (!doc) {
        const near = searchDocs(repo.docs, p, 3).map((h) => h.doc.path);
        return text(`No document at "${path}".${near.length ? ` Closest: ${[...new Set(near)].join(", ")}` : ""}${stamp()}`);
      }
      return text(`# ${doc.title}\n\n_${doc.description}_\n\n${doc.body}${stamp()}`);
    }
  );

  server.registerTool(
    "list_docs",
    {
      title: "List documents",
      description: "Every documentation page with its path, title, description and section.",
      inputSchema: {
        section: z.string().optional().describe("Filter: overview, getting-started, protocols, modules, architecture, contributing, mcp"),
      },
    },
    async ({ section }) => {
      const docs = section ? repo.docs.filter((d) => d.section === section) : repo.docs;
      if (!docs.length) return text(`No documents in section "${section}".${stamp()}`);
      const bySection = new Map<string, typeof docs>();
      for (const d of docs) bySection.set(d.section, [...(bySection.get(d.section) ?? []), d]);
      const out: string[] = [];
      for (const [s, list] of bySection) {
        out.push(`## ${s}`);
        for (const d of list) out.push(`- \`${d.path}\` — **${d.title}**: ${d.description}`);
      }
      return text(out.join("\n") + stamp());
    }
  );

  // ------------------------------------------------------------------- source

  server.registerTool(
    "search_source",
    {
      title: "Search library source",
      description:
        "Case-insensitive search over src/ (.h and .cpp). Returns matching lines with a little " +
        "context, headers first. For a declaration by name prefer get_api.",
      inputSchema: {
        query: z.string().min(1).describe("Identifier or phrase, e.g. 'MQTT_Send' or 'last_will'"),
        limit: z.number().int().min(1).max(50).optional().describe("Max hits, default 20"),
      },
    },
    async ({ query, limit }) => {
      const hits = searchSource(repo.sources, query, limit ?? 20);
      if (!hits.length) return text(`Nothing in src/ matched "${query}".${stamp()}`);
      const out = hits.map((h) => `${h.file}:${h.line}\n${fence("cpp", h.context.join("\n"))}`);
      return text(out.join("\n\n") + stamp());
    }
  );

  server.registerTool(
    "get_source",
    {
      title: "Get a source file",
      description:
        "Returns a file from src/, or a line range of it. Paths are relative to the repository " +
        "root, e.g. 'src/Core/MQTT.h'. Whole files can be long; prefer a range once you know where to look.",
      inputSchema: {
        path: z.string().min(1).describe("e.g. 'src/Core/Scheduler.h'"),
        from: z.number().int().min(1).optional().describe("First line, 1-based"),
        to: z.number().int().min(1).optional().describe("Last line, inclusive"),
      },
    },
    async ({ path, from, to }) => {
      const p = path.replace(/^\/+/, "").replace(/^(?!src\/)/, "src/");
      const file = repo.sources.find((f) => f.path === p) ?? repo.sources.find((f) => f.path.endsWith("/" + path));
      if (!file) return text(`No file "${path}" under src/. Try search_source.${stamp()}`);
      const start = Math.max(1, from ?? 1);
      const end = Math.min(file.lines.length, to ?? file.lines.length);
      const body = file.lines.slice(start - 1, end).join("\n");
      return text(`${file.path} (lines ${start}–${end} of ${file.lines.length})\n${fence("cpp", body)}${stamp()}`);
    }
  );

  server.registerTool(
    "get_api",
    {
      title: "Look up an API",
      description:
        "Finds where a function, method, class, struct, enum, macro or extern is declared and " +
        "returns the declaration with the doc comment above it. Exact identifier, e.g. 'MQTT_Publish', " +
        "'ManagedState', 'SchedulerRunMode', 'NM_ENABLE_TELEMETRY'.",
      inputSchema: { name: z.string().min(1).describe("The identifier") },
    },
    async ({ name }) => {
      const hits = findApi(repo.sources, name);
      if (!hits.length) {
        const near = searchSource(repo.sources, name, 5);
        return text(
          `No declaration named "${name}".` +
            (near.length ? `\n\nMentions:\n${near.map((h) => `- ${h.file}:${h.line}  ${h.text.trim()}`).join("\n")}` : "") +
            stamp()
        );
      }
      const out = hits.map((h) => {
        const c = h.comment ? h.comment + "\n\n" : "";
        return `**${h.file}:${h.line}**\n\n${c}${fence("cpp", h.declaration)}`;
      });
      return text(out.join("\n\n---\n\n") + stamp());
    }
  );

  // ----------------------------------------------------------------- examples

  server.registerTool(
    "get_example",
    {
      title: "Get an example project",
      description:
        "Without a name, lists the example projects under examples/. With a name, returns that " +
        "example's README and every file in it -- platformio.ini, main.cpp, the config headers.",
      inputSchema: { name: z.string().optional().describe("Example name, e.g. 'basic'") },
    },
    async ({ name }) => {
      if (!name) {
        if (!repo.examples.length) return text(`No examples in this checkout.${stamp()}`);
        const out = repo.examples.map((e) => `- **${e.name}** — ${e.readme.split(/\r?\n/).find((l) => l && !l.startsWith("#"))?.trim() ?? ""}`);
        return text(`Examples:\n${out.join("\n")}${stamp()}`);
      }
      const ex = repo.examples.find((e) => e.name === name);
      if (!ex) return text(`No example "${name}". Available: ${repo.examples.map((e) => e.name).join(", ") || "none"}.${stamp()}`);
      const files = ex.files.map((f) => `### ${f.path}\n${fence(langFor(f.path), f.content)}`);
      return text(`# examples/${ex.name}\n\n${ex.readme}\n\n${files.join("\n\n")}${stamp()}`);
    }
  );

  // ------------------------------------------------------------------ version

  server.registerTool(
    "get_version",
    {
      title: "Library version",
      description:
        "The library version from library.json, the Git branch and revision the server is serving, " +
        "and whether the working tree had uncommitted changes when it started.",
      inputSchema: {},
    },
    async () => {
      const v = repo.version;
      const lines = [
        `library: ${v.library}`,
        `branch: ${v.branch}`,
        `revision: ${v.revision}`,
        `dirty: ${v.dirty}`,
        `revision source: ${v.source}`,
        `loaded: ${repo.loadedAt.toISOString()}`,
        `documents: ${repo.docs.length} · source files: ${repo.sources.length} · examples: ${repo.examples.length}`,
        "",
        "Consumers on Git track the latest revision:",
        "  lib_deps = https://github.com/vtmattedi/NightMareNetwork.git",
        "Pin a release with a tag suffix: ...NightMareNetwork.git#v0.2.0",
      ];
      return text(lines.join("\n"));
    }
  );

  // --------------------------------------------------------------- resources

  server.registerResource(
    "docs",
    new ResourceTemplate("nightmare://docs/{+path}", {
      list: async () => ({
        resources: repo.docs.map((d) => ({
          uri: `nightmare://docs/${d.path}`,
          name: d.title,
          description: d.description,
          mimeType: "text/markdown",
        })),
      }),
    }),
    { title: "NightMare Network documentation", description: "Every documentation page, as markdown.", mimeType: "text/markdown" },
    async (uri, variables) => {
      const path = String(variables.path ?? "").replace(/\.md$/, "");
      const doc = repo.docs.find((d) => d.path === path);
      if (!doc) throw new Error(`No document at ${uri.href}`);
      return { contents: [{ uri: uri.href, mimeType: "text/markdown", text: `# ${doc.title}\n\n${doc.body}` }] };
    }
  );

  server.registerResource(
    "version",
    "nightmare://version",
    { title: "Library version", description: "Version and revision being served.", mimeType: "text/plain" },
    async (uri) => ({ contents: [{ uri: uri.href, mimeType: "text/plain", text: versionLine(repo.version) }] })
  );

  return server;
}
