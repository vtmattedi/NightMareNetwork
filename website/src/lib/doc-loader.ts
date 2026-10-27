import type { LoaderFunctionArgs } from "react-router-dom";
import type { DocContent } from "./sections";

export const slugOf = (params: { a?: string; b?: string }) => [params.a, params.b].filter(Boolean).join("/") || "overview";

/**
 * Runs at build time for every prerendered docs page; vite-react-ssg saves the
 * result as static JSON and, in the built client, replaces this loader with a
 * fetch of that file. The production client never executes the body below,
 * and the constant condition lets the bundler drop the content import from it.
 * The plain `vite` dev server has no SSG step, so there it runs in the browser.
 */
export async function docLoader({ params }: LoaderFunctionArgs): Promise<DocContent | null> {
  if (import.meta.env.PROD && !import.meta.env.SSR) return null;
  const { content } = await import("virtual:docs-content");
  return content[slugOf(params)] ?? null;
}
