// The sidebar sections, in order. Shared by the client and the build plugin,
// which rejects any document whose `section` is not listed here.

export const SECTIONS = [
  { id: "overview", label: "Overview" },
  { id: "getting-started", label: "Getting started" },
  { id: "protocols", label: "Protocols" },
  { id: "architecture", label: "Architecture" },
  { id: "modules", label: "Modules" },
  { id: "security", label: "Security" },
  { id: "contributing", label: "Contributing" },
  { id: "mcp", label: "MCP" },
] as const;

export type SectionId = (typeof SECTIONS)[number]["id"];

/** One document without its body: everything the sidebar, pager and head need. */
export interface DocMeta {
  /** "protocols/mqttp" -- relative to docs/, no extension. */
  slug: string;
  title: string;
  description: string;
  section: SectionId;
  order: number;
}

/** A document's rendered body, delivered to the page as route loader data. */
export interface DocContent {
  slug: string;
  html: string;
  headings: { depth: number; text: string; id: string }[];
}
