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
  /** Date of the last commit touching the file, ISO 8601; absent without full git history. */
  updated?: string;
}

/** What the build knows about the checkout it was built from. */
export interface SiteInfo {
  revision?: string;
  date?: string;
  changes: { sha: string; date: string; subject: string }[];
}

/** A document's rendered body, delivered to the page as route loader data. */
export interface DocContent {
  slug: string;
  html: string;
  headings: { depth: number; text: string; id: string }[];
}
