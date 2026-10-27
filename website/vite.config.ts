import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { docsPlugin, loadDocs } from "./build/docs.ts";
import { postbuild } from "./build/postbuild.ts";
import { buildSearchIndex } from "./build/search.ts";
import { SITE_URL } from "./src/site.ts";

const repoDir = resolve(fileURLToPath(new URL(".", import.meta.url)), "..");
const docsDir = resolve(repoDir, "docs");

export default defineConfig({
  plugins: [react(), docsPlugin(docsDir, repoDir)],
  // The docs live one level up, in the repository's docs/ folder, and the
  // site reads ../library.json for its version; the dev server has to be
  // allowed to read outside the site root.
  server: { fs: { allow: [".."] } },
  ssgOptions: {
    script: "async",
    dirStyle: "nested",
    onFinished: async (dir) => {
      postbuild(dir, SITE_URL, loadDocs(docsDir).map((d) => d.slug));
      await buildSearchIndex(dir);
    },
  },
});
