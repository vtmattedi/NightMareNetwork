// Loads the repository into memory: documentation, library source, examples
// and version. Everything the tools answer with comes from here, read once at
// startup. No database, no embeddings -- the corpus is a few hundred KB and a
// linear scan over it is faster than a network hop.

import { readFileSync, readdirSync, statSync, existsSync } from "node:fs";
import { join, relative, resolve, dirname, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { execSync } from "node:child_process";

export interface DocSection {
  /** Heading text, or the document title for the leading section. */
  heading: string;
  /** Anchor derived from the heading, GitHub-style. */
  anchor: string;
  /** Body text of this section, markdown, without the heading line. */
  body: string;
  /** 1-based line the heading is on. */
  line: number;
}

export interface Doc {
  /** Path relative to docs/, forward slashes, no extension: "protocols/mqttp". */
  path: string;
  title: string;
  description: string;
  section: string;
  order: number;
  /** Full markdown, front-matter stripped. */
  body: string;
  sections: DocSection[];
}

export interface SourceFile {
  /** Path relative to the repository root, forward slashes: "src/Core/MQTT.h". */
  path: string;
  lines: string[];
}

export interface Example {
  name: string;
  readme: string;
  files: { path: string; content: string }[];
}

export interface Version {
  library: string;
  revision: string;
  branch: string;
  dirty: boolean;
  /** Where "revision" came from: a REVISION file baked at image build, git, or nothing. */
  source: "file" | "git" | "unknown";
}

export interface Repo {
  root: string;
  docs: Doc[];
  sources: SourceFile[];
  examples: Example[];
  version: Version;
  loadedAt: Date;
}

/** Repository root: NM_REPO_ROOT, else two levels up from this file (mcp/dist -> repo). */
export function repoRoot(): string {
  if (process.env.NM_REPO_ROOT) return resolve(process.env.NM_REPO_ROOT);
  const here = dirname(fileURLToPath(import.meta.url));
  return resolve(here, "..", "..");
}

const toPosix = (p: string) => p.split(sep).join("/");

function walk(dir: string, accept: (file: string) => boolean, out: string[] = []): string[] {
  if (!existsSync(dir)) return out;
  for (const entry of readdirSync(dir)) {
    if (entry === "node_modules" || entry.startsWith(".")) continue;
    const full = join(dir, entry);
    const st = statSync(full);
    if (st.isDirectory()) walk(full, accept, out);
    else if (accept(full)) out.push(full);
  }
  return out;
}

/** Minimal front-matter: a leading `---` block of `key: value` lines. */
export function parseFrontMatter(raw: string): { meta: Record<string, string>; body: string } {
  const meta: Record<string, string> = {};
  if (!raw.startsWith("---")) return { meta, body: raw };
  const end = raw.indexOf("\n---", 3);
  if (end < 0) return { meta, body: raw };
  const block = raw.slice(3, end).trim();
  for (const line of block.split(/\r?\n/)) {
    const i = line.indexOf(":");
    if (i < 0) continue;
    meta[line.slice(0, i).trim()] = line.slice(i + 1).trim();
  }
  return { meta, body: raw.slice(end + 4).replace(/^\r?\n/, "") };
}

export function slugify(text: string): string {
  return text
    .toLowerCase()
    .replace(/`/g, "")
    .replace(/[^a-z0-9\s-]/g, "")
    .trim()
    .replace(/\s+/g, "-");
}

/** Splits a document on `##`/`###` headings. The text before the first heading is the lead section. */
function splitSections(title: string, body: string): DocSection[] {
  const lines = body.split(/\r?\n/);
  const sections: DocSection[] = [];
  let current: DocSection = { heading: title, anchor: "", body: "", line: 1 };
  let inFence = false;
  const buf: string[] = [];
  const flush = () => {
    current.body = buf.join("\n").trim();
    sections.push(current);
    buf.length = 0;
  };
  lines.forEach((line, i) => {
    if (/^\s*```/.test(line)) inFence = !inFence;
    const m = !inFence && /^(##{1,2})\s+(.+?)\s*$/.exec(line);
    if (m) {
      flush();
      current = { heading: m[2], anchor: slugify(m[2]), body: "", line: i + 1 };
    } else if (!(i === 0 && /^#\s/.test(line))) {
      buf.push(line);
    }
  });
  flush();
  return sections;
}

function loadDocs(root: string): Doc[] {
  const dir = join(root, "docs");
  return walk(dir, (f) => f.endsWith(".md"))
    .map((file) => {
      const raw = readFileSync(file, "utf8");
      const { meta, body } = parseFrontMatter(raw);
      const path = toPosix(relative(dir, file)).replace(/\.md$/, "");
      const title = meta.title ?? path;
      return {
        path,
        title,
        description: meta.description ?? "",
        section: meta.section ?? path.split("/")[0],
        order: Number(meta.order ?? 1000),
        body,
        sections: splitSections(title, body),
      };
    })
    .sort((a, b) => a.order - b.order || a.path.localeCompare(b.path));
}

function loadSources(root: string): SourceFile[] {
  const dir = join(root, "src");
  return walk(dir, (f) => /\.(h|hpp|cpp|c)$/.test(f))
    .map((file) => ({
      path: toPosix(relative(root, file)),
      lines: readFileSync(file, "utf8").split(/\r?\n/),
    }))
    .sort((a, b) => a.path.localeCompare(b.path));
}

function loadExamples(root: string): Example[] {
  const dir = join(root, "examples");
  if (!existsSync(dir)) return [];
  return readdirSync(dir)
    .filter((name) => statSync(join(dir, name)).isDirectory())
    .map((name) => {
      const base = join(dir, name);
      const files = walk(base, (f) => !/\.(png|jpg|bin|ttf)$/.test(f)).map((f) => ({
        path: toPosix(relative(base, f)),
        content: readFileSync(f, "utf8"),
      }));
      const readme = files.find((f) => f.path.toLowerCase() === "readme.md")?.content ?? "";
      return { name, readme, files: files.filter((f) => f.path.toLowerCase() !== "readme.md") };
    })
    .sort((a, b) => a.name.localeCompare(b.name));
}

function git(root: string, args: string): string | null {
  try {
    return execSync(`git ${args}`, { cwd: root, stdio: ["ignore", "pipe", "ignore"], timeout: 3000 })
      .toString()
      .trim();
  } catch {
    return null;
  }
}

function loadVersion(root: string): Version {
  let library = "unknown";
  try {
    library = JSON.parse(readFileSync(join(root, "library.json"), "utf8")).version ?? library;
  } catch {
    /* library.json is optional for the server, required for the library */
  }

  // An image build writes REVISION/BRANCH so the container does not need git.
  const revFile = join(root, "REVISION");
  if (existsSync(revFile)) {
    const branchFile = join(root, "BRANCH");
    return {
      library,
      revision: readFileSync(revFile, "utf8").trim() || "unknown",
      branch: existsSync(branchFile) ? readFileSync(branchFile, "utf8").trim() : "unknown",
      dirty: false,
      source: "file",
    };
  }

  const revision = git(root, "rev-parse --short HEAD");
  if (revision) {
    return {
      library,
      revision,
      branch: git(root, "rev-parse --abbrev-ref HEAD") ?? "unknown",
      dirty: (git(root, "status --porcelain") ?? "").length > 0,
      source: "git",
    };
  }
  return { library, revision: "unknown", branch: "unknown", dirty: false, source: "unknown" };
}

export function loadRepo(root = repoRoot()): Repo {
  return {
    root,
    docs: loadDocs(root),
    sources: loadSources(root),
    examples: loadExamples(root),
    version: loadVersion(root),
    loadedAt: new Date(),
  };
}

/** One line every tool result ends with, so an answer always says where it came from. */
export function versionLine(v: Version): string {
  const dirty = v.dirty ? " (uncommitted changes)" : "";
  return `NightMare Network ${v.library} · ${v.branch} @ ${v.revision}${dirty}`;
}
