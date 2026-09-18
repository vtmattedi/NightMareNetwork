// The list of documents, without their contents, from the virtual module the
// Vite config generates by walking ../docs. The route table uses it to
// prerender every page while the markdown itself stays in the docs chunk that
// only the docs page loads.

import { slugs } from "virtual:docs-index";

export { slugs };
export const topLevelSlugs = slugs.filter((s) => !s.includes("/"));
export const nestedSlugs = slugs.filter((s) => s.includes("/"));
