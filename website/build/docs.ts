// Build-time documentation pipeline. Reads ../docs, validates front matter,
// and renders every document to HTML once, in Node. The browser never
// downloads the markdown, the parser or the highlighter: a docs page gets its
// HTML prerendered and, on client navigation, as static loader JSON.
//
// Two virtual modules come out of it:
//   virtual:docs-index    metadata for every document (sidebar, pager, head)
//   virtual:docs-content  rendered HTML + headings; imported only by the
//                         server-side route loader, so it never reaches the
//                         client bundle

import { readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative, sep } from "node:path";
import type { Plugin } from "vite";
import type { Element, Root } from "hast";
import GithubSlugger from "github-slugger";
import { toString } from "mdast-util-to-string";
import rehypeHighlight from "rehype-highlight";
import rehypeSlug from "rehype-slug";
import rehypeStringify from "rehype-stringify";
import remarkGfm from "remark-gfm";
import remarkParse from "remark-parse";
import remarkRehype from "remark-rehype";
import { unified } from "unified";
import { visit } from "unist-util-visit";
// Only the grammars the documentation uses.
import bash from "highlight.js/lib/languages/bash";
import cpp from "highlight.js/lib/languages/cpp";
import ini from "highlight.js/lib/languages/ini";
import json from "highlight.js/lib/languages/json";
import plaintext from "highlight.js/lib/languages/plaintext";
import yaml from "highlight.js/lib/languages/yaml";
import { SECTIONS, type DocContent, type DocMeta, type SectionId } from "../src/lib/sections.ts";

const languages = { cpp, c: cpp, ini, json, bash, sh: bash, shell: bash, yaml, yml: yaml, text: plaintext, plaintext };
const sectionIds = new Set<string>(SECTIONS.map((s) => s.id));

export interface Doc extends DocMeta, DocContent {}

/**
 * Minimal front matter: a leading `---` block of `key: value` lines, split at
 * the first colon. Deliberately the same convention mcp/src/repo.ts parses, so
 * the site and the MCP can never read a document differently.
 */
function parseFrontMatter(text: string): { meta: Record<string, string>; body: string } | null {
  if (!text.startsWith("---")) return null;
  const end = text.indexOf("\n---", 3);
  if (end < 0) return null;
  const meta: Record<string, string> = {};
  for (const line of text.slice(3, end).trim().split(/\r?\n/)) {
    const i = line.indexOf(":");
    if (i > 0) meta[line.slice(0, i).trim()] = line.slice(i + 1).trim();
  }
  return { meta, body: text.slice(end + 4).replace(/^\r?\n/, "") };
}

/**
 * Markdown sources link with paths relative to docs/, ending in .md. Rewrite
 * those to the site's routes; leave anchors and absolute URLs alone.
 */
export function docHref(href: string, slug: string): string {
  if (!/\.md(?:#.*)?$/i.test(href) || /^(?:[a-z][a-z\d+.-]*:|\/\/)/i.test(href)) return href;
  const [sourcePath, fragment = ""] = href.split("#", 2);
  const parts = sourcePath.startsWith("/") ? [] : slug.split("/").slice(0, -1);
  for (const part of sourcePath.replace(/^\/docs\//, "").replace(/^\//, "").replace(/\.md$/i, "").split("/")) {
    if (part === "..") parts.pop();
    else if (part && part !== ".") parts.push(part);
  }
  return `/docs/${parts.join("/")}${fragment ? `#${fragment}` : ""}`;
}

function rehypeDocLinks(slug: string) {
  return (tree: Root) => {
    visit(tree, "element", (node: Element) => {
      if (node.tagName !== "a" || typeof node.properties.href !== "string") return;
      const href = docHref(node.properties.href, slug);
      node.properties.href = href;
      if (/^https?:\/\//.test(href)) {
        node.properties.target = "_blank";
        node.properties.rel = ["noreferrer"];
      }
    });
  };
}

function render(slug: string, body: string): DocContent {
  const processor = unified()
    .use(remarkParse)
    .use(remarkGfm)
    .use(remarkRehype)
    .use(rehypeSlug)
    .use(rehypeHighlight, { languages, detect: false })
    .use(rehypeDocLinks, slug)
    .use(rehypeStringify);
  const mdast = processor.parse(body);
  const html = processor.stringify(processor.runSync(mdast));

  // Same slugger, same order, same text as rehype-slug above, so the
  // "On this page" ids match the rendered ones.
  const headings: DocContent["headings"] = [];
  const slugger = new GithubSlugger();
  visit(mdast, "heading", (node) => {
    const text = toString(node);
    const id = slugger.slug(text);
    if (node.depth === 2 || node.depth === 3) headings.push({ depth: node.depth, text, id });
  });
  return { slug, html, headings };
}

function walk(dir: string, out: string[] = []): string[] {
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) walk(full, out);
    else if (entry.endsWith(".md")) out.push(full);
  }
  return out;
}

/** Reads and renders the whole corpus. Throws one error listing every invalid document. */
export function loadDocs(docsDir: string): Doc[] {
  const problems: string[] = [];
  const docs: Doc[] = [];
  for (const file of walk(docsDir)) {
    const slug = relative(docsDir, file).split(sep).join("/").replace(/\.md$/, "");
    const parsed = parseFrontMatter(readFileSync(file, "utf8"));
    if (!parsed) {
      problems.push(`${slug}.md: no front matter`);
      continue;
    }
    const { meta, body } = parsed;
    const missing = ["title", "description", "section"].filter((k) => !meta[k]);
    if (missing.length) problems.push(`${slug}.md: missing ${missing.join(", ")}`);
    if (meta.section && !sectionIds.has(meta.section))
      problems.push(`${slug}.md: section "${meta.section}" is not one of ${[...sectionIds].join(", ")} (website/src/lib/sections.ts)`);
    if (meta.order !== undefined && !Number.isFinite(Number(meta.order))) problems.push(`${slug}.md: order "${meta.order}" is not a number`);
    if (missing.length) continue;
    docs.push({
      title: meta.title,
      description: meta.description,
      section: meta.section as SectionId,
      order: Number(meta.order ?? 1000),
      ...render(slug, body),
    });
  }
  if (problems.length) throw new Error(`Invalid documentation front matter:\n  ${problems.join("\n  ")}`);

  const rank = (s: string) => SECTIONS.findIndex((x) => x.id === s);
  return docs.sort((a, b) => rank(a.section) - rank(b.section) || a.order - b.order || a.slug.localeCompare(b.slug));
}

export function docsPlugin(docsDir: string): Plugin {
  const INDEX = "virtual:docs-index";
  const CONTENT = "virtual:docs-content";
  let cache: Doc[] | undefined;
  const docs = () => (cache ??= loadDocs(docsDir));

  return {
    name: "nightmare-docs",
    resolveId(source) {
      return source === INDEX || source === CONTENT ? "\0" + source : undefined;
    },
    load(id) {
      if (id === "\0" + INDEX) {
        const index: DocMeta[] = docs().map(({ slug, title, description, section, order }) => ({ slug, title, description, section, order }));
        return `export const docs = ${JSON.stringify(index)};`;
      }
      if (id === "\0" + CONTENT) {
        const content = Object.fromEntries(docs().map(({ slug, html, headings }) => [slug, { slug, html, headings }]));
        return `export const content = ${JSON.stringify(content)};`;
      }
      return undefined;
    },
    configureServer(server) {
      // Dev: re-render on any change under docs/ and reload the page.
      server.watcher.add(docsDir);
      server.watcher.on("all", (_event, file) => {
        if (!file.startsWith(docsDir) || !file.endsWith(".md")) return;
        cache = undefined;
        for (const id of ["\0" + INDEX, "\0" + CONTENT]) {
          const mod = server.moduleGraph.getModuleById(id);
          if (mod) server.moduleGraph.invalidateModule(mod);
        }
        server.ws.send({ type: "full-reload" });
      });
    },
  };
}
