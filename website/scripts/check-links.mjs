#!/usr/bin/env node
// Checks the built site: every internal link resolves to a prerendered page or
// file, every #fragment exists on its target page, and every page has a
// non-empty <title>. Exits non-zero with a list of problems.
//
//   node scripts/check-links.mjs [dist]

import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative, sep } from "node:path";

const dist = process.argv[2] ?? "dist";

function htmlFiles(dir, out = []) {
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) htmlFiles(full, out);
    else if (entry.endsWith(".html")) out.push(full);
  }
  return out;
}

const decode = (s) => s.replace(/&amp;/g, "&").replace(/&quot;/g, '"').replace(/&#x27;|&#39;/g, "'");
const pageOf = (file) => "/" + relative(dist, file).split(sep).join("/").replace(/index\.html$/, "");

const pages = new Map();
for (const file of htmlFiles(dist)) {
  const html = readFileSync(file, "utf8");
  // Only the visible document: hydration data in <script> is not markup.
  const body = html.replace(/<script\b[\s\S]*?<\/script>/g, "");
  pages.set(pageOf(file), {
    html: body,
    ids: new Set([...body.matchAll(/\sid="([^"]*)"/g)].map((m) => decode(m[1]))),
  });
}

/** The page a site path is served from, or the file it names; undefined if nothing is there. */
function resolve(path) {
  const dir = path.endsWith("/") ? path : path + "/";
  if (pages.has(dir)) return { page: dir };
  if (existsSync(join(dist, path)) && statSync(join(dist, path)).isFile()) return { file: path };
  return undefined;
}

const problems = [];
for (const [page, { html, ids }] of pages) {
  if (!/<title\b[^>]*>[^<]+<\/title>/.test(html)) problems.push(`${page}: missing <title>`);
  for (const [, raw] of html.matchAll(/<a\b[^>]*\shref="([^"]*)"/g)) {
    const href = decode(raw);
    if (/^(?:[a-z][a-z\d+.-]*:|\/\/)/i.test(href)) continue; // external
    const [path, fragment] = href.split("#", 2);
    if (!path) {
      if (fragment && !ids.has(decodeURIComponent(fragment))) problems.push(`${page}: no element with id "${fragment}"`);
      continue;
    }
    if (!path.startsWith("/")) {
      problems.push(`${page}: relative link "${href}"`);
      continue;
    }
    const target = resolve(path);
    if (!target) problems.push(`${page}: broken link "${href}"`);
    else if (fragment && target.page && !pages.get(target.page).ids.has(decodeURIComponent(fragment)))
      problems.push(`${page}: "${href}" -- no element with id "${fragment}" on ${target.page}`);
  }
}

if (problems.length) {
  console.error(`${problems.length} link problem(s):\n  ${problems.join("\n  ")}`);
  process.exit(1);
}
console.log(`Checked ${pages.size} pages: all links and anchors resolve.`);
