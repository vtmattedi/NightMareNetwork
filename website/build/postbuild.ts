// After every page is prerendered: sitemap.xml, robots.txt, and two fixes to
// each HTML file that vite-react-ssg does not offer a hook for.
//
// - <meta charset> is moved to the very start of <head>. The SSG inserts the
//   page's <title> and meta tags ahead of it, and the charset declaration has
//   to come before any text the parser reads.
// - A Content-Security-Policy <meta> pins every inline script by hash. Each
//   page carries its own inline scripts (the theme script and the router's
//   hydration data, which differs per page), so the hashes are per page and
//   cannot live in an nginx header. frame-ancestors cannot be set from a
//   <meta>; nginx sends that one.

import { createHash } from "node:crypto";
import { readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join, relative, sep } from "node:path";

function htmlFiles(dir: string, out: string[] = []): string[] {
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) htmlFiles(full, out);
    else if (entry.endsWith(".html")) out.push(full);
  }
  return out;
}

function csp(html: string): string {
  const hashes = [...html.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g)].map(
    ([, body]) => `'sha256-${createHash("sha256").update(body).digest("base64")}'`,
  );
  return [
    "default-src 'self'",
    // wasm-unsafe-eval: Pagefind's search runs as WebAssembly. It allows
    // compiling wasm only, not eval() of JavaScript.
    `script-src 'self' 'wasm-unsafe-eval' ${[...new Set(hashes)].join(" ")}`,
    "style-src 'self'",
    "img-src 'self' data:",
    "font-src 'self'",
    "connect-src 'self'",
    "object-src 'none'",
    "base-uri 'self'",
    "form-action 'self'",
  ].join("; ");
}

export function postbuild(outDir: string, siteUrl: string, slugs: string[]) {
  for (const file of htmlFiles(outDir)) {
    let html = readFileSync(file, "utf8");
    html = html.replace(/\s*<meta charset="UTF-8">/i, "");
    html = html.replace(
      "<head>",
      `<head><meta charset="UTF-8"><meta http-equiv="Content-Security-Policy" content="${csp(html)}">`,
    );
    writeFileSync(file, html);
  }

  // Every prerendered page except /404/ and the /docs/ alias of the overview.
  const pages = htmlFiles(outDir)
    .map((f) => "/" + relative(outDir, f).split(sep).join("/").replace(/index\.html$/, ""))
    .filter((p) => p !== "/404/" && p !== "/docs/");
  const known = new Set(["/", ...slugs.map((s) => `/docs/${s}/`)]);
  const urls = pages.filter((p) => known.has(p)).sort();
  writeFileSync(
    join(outDir, "sitemap.xml"),
    `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n${urls
      .map((u) => `  <url><loc>${siteUrl}${u}</loc></url>`)
      .join("\n")}\n</urlset>\n`,
  );
  writeFileSync(join(outDir, "robots.txt"), `User-agent: *\nAllow: /\n\nSitemap: ${siteUrl}/sitemap.xml\n`);
}
