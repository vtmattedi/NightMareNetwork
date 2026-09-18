// The documentation corpus, read from ../docs at build time through Vite's
// glob import. Same front-matter convention the MCP server parses, so the
// website and the AI see one set of documents.

const raw = import.meta.glob<string>("../../../docs/**/*.md", {
  query: "?raw",
  import: "default",
  eager: true,
});

export interface Doc {
  /** "protocols/mqttp" -- relative to docs/, no extension. */
  slug: string;
  title: string;
  description: string;
  section: string;
  order: number;
  body: string;
  headings: { depth: number; text: string; id: string }[];
}

export const SECTION_ORDER = ["overview", "getting-started", "protocols", "architecture", "modules", "mcp"];

export const SECTION_LABEL: Record<string, string> = {
  overview: "Overview",
  "getting-started": "Getting started",
  protocols: "Protocols",
  architecture: "Architecture",
  modules: "Modules",
  mcp: "MCP",
};

function parseFrontMatter(text: string): { meta: Record<string, string>; body: string } {
  const meta: Record<string, string> = {};
  if (!text.startsWith("---")) return { meta, body: text };
  const end = text.indexOf("\n---", 3);
  if (end < 0) return { meta, body: text };
  for (const line of text.slice(3, end).trim().split(/\r?\n/)) {
    const i = line.indexOf(":");
    if (i > 0) meta[line.slice(0, i).trim()] = line.slice(i + 1).trim();
  }
  return { meta, body: text.slice(end + 4).replace(/^\r?\n/, "") };
}

/** Same algorithm rehype-slug uses for ids (github-slugger), close enough for our headings. */
export function slugify(text: string): string {
  return text
    .toLowerCase()
    .replace(/`/g, "")
    .replace(/[^\p{L}\p{N}\s-]/gu, "")
    .trim()
    .replace(/\s+/g, "-");
}

function headingsOf(body: string) {
  const out: Doc["headings"] = [];
  let fence = false;
  for (const line of body.split(/\r?\n/)) {
    if (/^\s*```/.test(line)) fence = !fence;
    if (fence) continue;
    const m = /^(#{2,3})\s+(.+?)\s*$/.exec(line);
    if (m) out.push({ depth: m[1].length, text: m[2], id: slugify(m[2]) });
  }
  return out;
}

export const docs: Doc[] = Object.entries(raw)
  .map(([path, text]) => {
    const slug = path.replace(/^.*\/docs\//, "").replace(/\.md$/, "");
    const { meta, body } = parseFrontMatter(text);
    return {
      slug,
      title: meta.title ?? slug,
      description: meta.description ?? "",
      section: meta.section ?? slug.split("/")[0],
      order: Number(meta.order ?? 1000),
      body,
      headings: headingsOf(body),
    };
  })
  .sort((a, b) => {
    const sa = SECTION_ORDER.indexOf(a.section);
    const sb = SECTION_ORDER.indexOf(b.section);
    return (sa < 0 ? 99 : sa) - (sb < 0 ? 99 : sb) || a.order - b.order || a.slug.localeCompare(b.slug);
  });

export const docBySlug = (slug: string) => docs.find((d) => d.slug === slug);

export const sections = SECTION_ORDER.map((id) => ({
  id,
  label: SECTION_LABEL[id] ?? id,
  docs: docs.filter((d) => d.section === id),
})).filter((s) => s.docs.length);

/** Every slug, for prerendering. Split by depth because the routes are. */
export const topLevelSlugs = docs.filter((d) => !d.slug.includes("/")).map((d) => d.slug);
export const nestedSlugs = docs.filter((d) => d.slug.includes("/")).map((d) => d.slug);

export function neighbours(slug: string) {
  const i = docs.findIndex((d) => d.slug === slug);
  return { prev: i > 0 ? docs[i - 1] : undefined, next: i >= 0 && i < docs.length - 1 ? docs[i + 1] : undefined };
}
