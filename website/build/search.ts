// Full-text search index, built from the prerendered HTML by Pagefind. Only
// elements marked data-pagefind-body (the docs article) are indexed. The
// browser loads /pagefind/pagefind.js on the first search; nothing before.

import { join } from "node:path";
import * as pagefind from "pagefind";

export async function buildSearchIndex(outDir: string) {
  const { index, errors } = await pagefind.createIndex({});
  if (!index) throw new Error(`Pagefind: ${errors.join("; ")}`);
  const added = await index.addDirectory({ path: outDir });
  if (added.errors.length) throw new Error(`Pagefind: ${added.errors.join("; ")}`);
  const written = await index.writeFiles({ outputPath: join(outDir, "pagefind") });
  if (written.errors.length) throw new Error(`Pagefind: ${written.errors.join("; ")}`);
  console.log(`[pagefind] indexed ${added.page_count} pages`);
  await pagefind.close();
}
