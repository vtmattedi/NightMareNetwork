import { useCallback, useEffect, useRef, useState, type KeyboardEvent } from "react";
import { Link, useNavigate } from "react-router-dom";
import { Search as SearchIcon } from "lucide-react";

// The subset of Pagefind's browser API used here.
interface PagefindResultData {
  url: string;
  excerpt: string;
  meta: { title?: string };
  sub_results: { title: string; url: string; excerpt: string; locations?: number[] }[];
}
interface Pagefind {
  options(o: Record<string, unknown>): Promise<void>;
  debouncedSearch(q: string, o?: object, ms?: number): Promise<{ results: { data(): Promise<PagefindResultData> }[] } | null>;
}

// A variable, not a literal, so the bundler leaves this import to the browser:
// the file is written by Pagefind after the bundle exists.
const PAGEFIND_URL = "/pagefind/pagefind.js";
let pagefind: Promise<Pagefind> | undefined;
/** Loaded on first use. The index only exists in a production build. */
function loadPagefind(): Promise<Pagefind> {
  pagefind ??= import(/* @vite-ignore */ PAGEFIND_URL).then(async (pf: Pagefind) => {
    await pf.options({ excerptLength: 24 });
    return pf;
  });
  return pagefind;
}

interface Hit {
  url: string;
  title: string;
  excerpt: string;
}

/**
 * Pagefind reports the built file's URL ("/docs/mcp/#x"). Client routes and
 * vite-react-ssg's loader-data manifest are keyed without the trailing slash.
 */
const routeOf = (url: string) => url.replace(/\/(?=#|$)/, "") || "/";

/** Pagefind's own excerpts are escaped text plus <mark>; keep only that. */
const safeExcerpt = (html: string) => html.replace(/<(?!\/?mark>)[^>]*>/g, "");

export function Search() {
  const dialog = useRef<HTMLDialogElement>(null);
  const input = useRef<HTMLInputElement>(null);
  const navigate = useNavigate();
  const [query, setQuery] = useState("");
  const [hits, setHits] = useState<Hit[]>([]);
  const [status, setStatus] = useState<"idle" | "loading" | "unavailable">("idle");

  const open = useCallback(() => {
    dialog.current?.showModal();
    input.current?.select();
    loadPagefind().catch(() => setStatus("unavailable"));
  }, []);
  const close = () => dialog.current?.close();

  // "/" or Ctrl/Cmd+K from anywhere outside a text field.
  useEffect(() => {
    const onKey = (e: globalThis.KeyboardEvent) => {
      const typing = e.target instanceof HTMLElement && e.target.closest("input, textarea, [contenteditable]");
      if ((e.key === "k" && (e.metaKey || e.ctrlKey)) || (e.key === "/" && !typing && !e.metaKey && !e.ctrlKey)) {
        e.preventDefault();
        open();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [open]);

  useEffect(() => {
    const q = query.trim();
    if (!q) {
      setHits([]);
      return;
    }
    let stale = false;
    setStatus((s) => (s === "unavailable" ? s : "loading"));
    loadPagefind()
      .then(async (pf) => {
        const search = await pf.debouncedSearch(q, {}, 120);
        if (!search || stale) return; // superseded by a newer query
        const data = await Promise.all(search.results.slice(0, 8).map((r) => r.data()));
        if (stale) return;
        setHits(
          data.map((d) => {
            // Prefer a section whose heading names every query word, then the
            // section with the most matches, over the page top. The page's own
            // h1 is a sub-result too; that one means "the page".
            const words = q.toLowerCase().split(/\s+/);
            const named = (t: string) => words.every((w) => t.toLowerCase().includes(w));
            const sub = d.sub_results
              .filter((s) => s.url.includes("#") && s.title !== d.meta.title)
              .sort((x, y) => Number(named(y.title)) - Number(named(x.title)) || (y.locations?.length ?? 0) - (x.locations?.length ?? 0))[0];
            return {
              url: routeOf((sub ?? d).url),
              title: sub && sub.title !== d.meta.title ? `${d.meta.title} › ${sub.title}` : d.meta.title ?? d.url,
              excerpt: safeExcerpt((sub ?? d).excerpt),
            };
          }),
        );
        setStatus("idle");
      })
      .catch(() => setStatus("unavailable"));
    return () => {
      stale = true;
    };
  }, [query]);

  const onInputKey = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "Enter" && hits[0]) {
      close();
      navigate(hits[0].url);
    }
  };

  return (
    <>
      <button type="button" className="search-btn" onClick={open} aria-label="Search the documentation" title="Search (/)">
        <SearchIcon size={17} aria-hidden="true" />
        <span className="search-label">Search</span>
        <kbd>/</kbd>
      </button>
      <dialog
        ref={dialog}
        className="search-dialog"
        aria-label="Search the documentation"
        onClick={(e) => e.target === dialog.current && close()}
      >
        <div className="search-box">
          <SearchIcon size={18} aria-hidden="true" />
          <input
            ref={input}
            type="search"
            placeholder="Search the documentation"
            aria-label="Search the documentation"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            onKeyDown={onInputKey}
          />
          <kbd>Esc</kbd>
        </div>
        {status === "unavailable" ? (
          <p className="search-note">Search is built with the production site (npm run build).</p>
        ) : query.trim() && !hits.length && status !== "loading" ? (
          <p className="search-note">No results for “{query.trim()}”.</p>
        ) : (
          <ul className="search-results">
            {hits.map((h) => (
              <li key={h.url}>
                <Link to={h.url} onClick={close}>
                  <strong>{h.title}</strong>
                  <span dangerouslySetInnerHTML={{ __html: h.excerpt }} />
                </Link>
              </li>
            ))}
          </ul>
        )}
      </dialog>
    </>
  );
}
