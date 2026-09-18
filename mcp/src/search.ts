// Ranking without a vector store. Documents are scored by term overlap with a
// weight for where the term appears (title > heading > body), and source files
// by matching lines. Good enough for a corpus this size, and every hit points
// at a real location the model can then fetch whole.

import type { Doc, DocSection, SourceFile } from "./repo.js";

const STOP = new Set([
  "the", "a", "an", "and", "or", "of", "to", "in", "on", "is", "it", "for", "with",
  "how", "does", "do", "what", "which", "that", "this", "are", "be", "as", "at", "by",
]);

export function tokenize(text: string): string[] {
  return text
    .toLowerCase()
    .split(/[^a-z0-9_]+/)
    .filter((t) => t.length > 1 && !STOP.has(t));
}

export interface DocHit {
  doc: Doc;
  section: DocSection;
  score: number;
  snippet: string;
}

function countOccurrences(haystack: string, needle: string): number {
  let n = 0;
  let i = 0;
  while ((i = haystack.indexOf(needle, i)) >= 0) {
    n++;
    i += needle.length;
  }
  return n;
}

/** A window of text around the first query term, with the term intact. */
function snippetFor(body: string, terms: string[], width = 220): string {
  const lower = body.toLowerCase();
  let at = -1;
  for (const t of terms) {
    const i = lower.indexOf(t);
    if (i >= 0 && (at < 0 || i < at)) at = i;
  }
  if (at < 0) return body.slice(0, width).replace(/\s+/g, " ").trim();
  const start = Math.max(0, at - width / 3);
  const end = Math.min(body.length, at + (width * 2) / 3);
  const text = body.slice(start, end).replace(/\s+/g, " ").trim();
  return (start > 0 ? "…" : "") + text + (end < body.length ? "…" : "");
}

export function searchDocs(docs: Doc[], query: string, limit = 8): DocHit[] {
  const terms = tokenize(query);
  if (!terms.length) return [];
  const phrase = query.toLowerCase().trim();
  const hits: DocHit[] = [];

  for (const doc of docs) {
    const titleL = doc.title.toLowerCase();
    for (const section of doc.sections) {
      const headingL = section.heading.toLowerCase();
      const bodyL = section.body.toLowerCase();
      let score = 0;
      for (const t of terms) {
        if (titleL.includes(t)) score += 5;
        if (headingL.includes(t)) score += 4;
        const inBody = countOccurrences(bodyL, t);
        if (inBody) score += 1 + Math.min(inBody, 5) * 0.5;
      }
      // Every term present somewhere in the section: a coherent match, not a scatter.
      if (terms.every((t) => titleL.includes(t) || headingL.includes(t) || bodyL.includes(t))) score *= 1.5;
      if (phrase.length > 3 && (bodyL.includes(phrase) || headingL.includes(phrase))) score += 6;
      if (score > 0) hits.push({ doc, section, score, snippet: snippetFor(section.body, terms) });
    }
  }

  return hits.sort((a, b) => b.score - a.score).slice(0, limit);
}

export interface SourceHit {
  file: string;
  line: number;
  text: string;
  context: string[];
}

export function searchSource(files: SourceFile[], query: string, limit = 20, contextLines = 2): SourceHit[] {
  const q = query.trim();
  if (!q) return [];
  const terms = tokenize(q);
  const literal = q.toLowerCase();
  const hits: SourceHit[] = [];

  for (const file of files) {
    file.lines.forEach((line, i) => {
      const l = line.toLowerCase();
      const match = l.includes(literal) || (terms.length > 1 && terms.every((t) => l.includes(t)));
      if (!match) return;
      const from = Math.max(0, i - contextLines);
      const to = Math.min(file.lines.length, i + contextLines + 1);
      hits.push({ file: file.path, line: i + 1, text: line, context: file.lines.slice(from, to) });
    });
  }

  // Headers first, then declarations over uses, then by path.
  const rank = (h: SourceHit) =>
    (h.file.endsWith(".h") ? 0 : 1) * 10 + (/^\s*(\/\/|\*)/.test(h.text) ? 1 : 0);
  return hits.sort((a, b) => rank(a) - rank(b) || a.file.localeCompare(b.file) || a.line - b.line).slice(0, limit);
}

export interface ApiHit {
  file: string;
  line: number;
  declaration: string;
  comment: string;
}

/**
 * Finds where a name is declared in the headers: a function, method, class,
 * struct, enum, macro or extern. Returns the declaration line(s) and the
 * comment block directly above, which is where the library keeps its API notes.
 */
export function findApi(files: SourceFile[], name: string): ApiHit[] {
  const n = name.trim();
  if (!n) return [];
  const esc = n.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const patterns = [
    new RegExp(`\\b${esc}\\s*\\(`),                              // function or method
    new RegExp(`^\\s*#define\\s+${esc}\\b`),                     // macro
    new RegExp(`\\b(class|struct|enum)\\s+${esc}\\b`),           // type
    new RegExp(`\\bextern\\b[^;]*\\b${esc}\\b`),                 // extern object
    new RegExp(`\\btypedef\\b[^;]*\\b${esc}\\b`),                // typedef
  ];
  const hits: ApiHit[] = [];

  for (const file of files) {
    const isHeader = /\.(h|hpp)$/.test(file.path);
    file.lines.forEach((line, i) => {
      if (!patterns.some((p) => p.test(line))) return;
      // Skip plain call sites in .cpp files: a declaration has a return type or keyword before the name.
      if (!isHeader && !/^\s*(\w[\w:<>*&\s]*\s+[*&]?\w+\s*\(|#define|class|struct|enum|extern|typedef)/.test(line)) return;

      // Multi-line declaration: read forward to the terminating ';' or '{'.
      let decl = line.trim();
      let j = i;
      while (!/[;{]\s*(\/\/.*)?$/.test(file.lines[j]) && j < i + 6 && j + 1 < file.lines.length) {
        j++;
        decl += " " + file.lines[j].trim();
      }

      // Comment block directly above: /// lines, // lines, or a /** ... */ block.
      const comment: string[] = [];
      let k = i - 1;
      while (k >= 0 && /^\s*(\/\/\/?|\*|\/\*\*?)/.test(file.lines[k])) {
        comment.unshift(file.lines[k].replace(/^\s*(\/\/\/?|\/\*\*?|\*\/|\*)\s?/, "").trimEnd());
        if (/^\s*\/\*/.test(file.lines[k])) break;
        k--;
      }

      hits.push({ file: file.path, line: i + 1, declaration: decl, comment: comment.join("\n").trim() });
    });
  }

  return hits.sort((a, b) => (a.file.endsWith(".h") ? 0 : 1) - (b.file.endsWith(".h") ? 0 : 1) || a.file.localeCompare(b.file));
}
