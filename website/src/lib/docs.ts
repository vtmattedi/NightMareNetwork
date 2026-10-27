// Document metadata for the sidebar, pager and head. The bodies are not here:
// they are rendered at build time and reach a page as route loader data.

import { docs } from "virtual:docs-index";
import { SECTIONS } from "./sections";

export { docs };

export const docBySlug = (slug: string) => docs.find((d) => d.slug === slug);

export const sections = SECTIONS.map((s) => ({ ...s, docs: docs.filter((d) => d.section === s.id) })).filter((s) => s.docs.length);

export const sectionLabel = (id: string) => SECTIONS.find((s) => s.id === id)?.label ?? id;

export const topLevelSlugs = docs.filter((d) => !d.slug.includes("/")).map((d) => d.slug);
export const nestedSlugs = docs.filter((d) => d.slug.includes("/")).map((d) => d.slug);

export function neighbours(slug: string) {
  const i = docs.findIndex((d) => d.slug === slug);
  return { prev: i > 0 ? docs[i - 1] : undefined, next: i >= 0 && i < docs.length - 1 ? docs[i + 1] : undefined };
}
