import { defineConfig, type Plugin } from "vite";
import react from "@vitejs/plugin-react";
import { readdirSync, statSync } from "node:fs";
import { join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const docsDir = resolve(fileURLToPath(new URL(".", import.meta.url)), "..", "docs");

/**
 * `virtual:docs-index` -- the list of documentation slugs, read from the
 * filesystem at build time. The route table needs every slug to prerender the
 * docs pages, but must not pull the markdown itself into the landing chunk;
 * a glob would either load the content (eager) or emit a chunk per file
 * (lazy), so the list is generated here instead.
 */
function docsIndex(): Plugin {
  const id = "virtual:docs-index";
  const resolved = "\0" + id;
  const walk = (dir: string, out: string[] = []): string[] => {
    for (const entry of readdirSync(dir)) {
      const full = join(dir, entry);
      if (statSync(full).isDirectory()) walk(full, out);
      else if (entry.endsWith(".md")) out.push(full);
    }
    return out;
  };
  return {
    name: "nightmare-docs-index",
    resolveId(source) {
      return source === id ? resolved : undefined;
    },
    load(moduleId) {
      if (moduleId !== resolved) return undefined;
      const slugs = walk(docsDir)
        .map((f) => relative(docsDir, f).split(sep).join("/").replace(/\.md$/, ""))
        .sort();
      return `export const slugs = ${JSON.stringify(slugs)};`;
    },
  };
}

export default defineConfig({
  plugins: [react(), docsIndex()],
  // The docs live one level up, in the repository's docs/ folder; the dev
  // server has to be allowed to read outside the site root.
  server: { fs: { allow: [".."] } },
  ssgOptions: {
    script: "async",
    dirStyle: "nested",
  },
});
