import { useEffect, useState, type MouseEvent } from "react";
import { Link, NavLink, useLoaderData, useNavigate } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { ChevronDown, ChevronLeft, ChevronRight } from "lucide-react";
import { docBySlug, neighbours, sectionLabel, sections } from "../lib/docs";
import type { DocContent } from "../lib/sections";
import { absolute, SITE_NAME } from "../site";
import { NotFound } from "./NotFound";

/** The id of the last h2 scrolled up to the header, for the "On this page" highlight. */
function useActiveHeading(ids: string[]): string | undefined {
  const [active, setActive] = useState<string>();
  const key = ids.join(" ");
  useEffect(() => {
    const targets = ids.map((id) => document.getElementById(id)).filter((el): el is HTMLElement => !!el);
    let frame = 0;
    const update = () => {
      frame = 0;
      // 76px is the sticky header plus scroll-padding; a little slack so a
      // heading jumped to from the TOC counts as current.
      const passed = targets.filter((t) => t.getBoundingClientRect().top <= 120);
      setActive(passed.length ? passed[passed.length - 1].id : undefined);
    };
    const onScroll = () => {
      if (!frame) frame = requestAnimationFrame(update);
    };
    update();
    window.addEventListener("scroll", onScroll, { passive: true });
    return () => {
      window.removeEventListener("scroll", onScroll);
      cancelAnimationFrame(frame);
    };
  }, [key]);
  return active;
}

export function DocPage() {
  const content = useLoaderData() as DocContent | null;
  const navigate = useNavigate();
  const [navOpen, setNavOpen] = useState(false);
  const pageHeadings = content?.headings.filter((h) => h.depth === 2) ?? [];
  const activeHeading = useActiveHeading(pageHeadings.map((h) => h.id));
  useEffect(() => setNavOpen(false), [content?.slug]);

  const doc = content && docBySlug(content.slug);
  if (!content || !doc) return <NotFound />;
  const { prev, next } = neighbours(doc.slug);
  const url = absolute(`/docs/${doc.slug}/`);

  // The body is prerendered HTML: its internal links are plain anchors and its
  // copy buttons are plain buttons. Both are handled here, by delegation.
  const onBodyClick = (e: MouseEvent<HTMLDivElement>) => {
    const target = e.target as HTMLElement;
    const copy = target.closest<HTMLButtonElement>(".copy-btn");
    if (copy) {
      const code = copy.parentElement?.querySelector("pre")?.innerText ?? "";
      navigator.clipboard?.writeText(code.replace(/\n$/, "")).then(
        () => {
          copy.textContent = "Copied";
          setTimeout(() => (copy.textContent = "Copy"), 1500);
        },
        () => (copy.textContent = "Press Ctrl+C"),
      );
      return;
    }
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
    const a = target.closest("a");
    const href = a?.getAttribute("href");
    if (!a || !href || a.target || !href.startsWith("/")) return;
    e.preventDefault();
    navigate(href);
  };

  return (
    <div className="wrap docs">
      <Head>
        <title>{`${doc.title} · ${SITE_NAME}`}</title>
        <meta name="description" content={doc.description} />
        <link rel="canonical" href={url} />
        <meta property="og:type" content="article" />
        <meta property="og:url" content={url} />
        <meta property="og:title" content={`${doc.title} · ${SITE_NAME}`} />
        <meta property="og:description" content={doc.description} />
      </Head>

      {/* Phones: the section list is a drawer above the article instead of a screen of links before it. */}
      <button
        type="button"
        className="side-toggle"
        aria-expanded={navOpen}
        aria-controls="docs-nav"
        onClick={() => setNavOpen((o) => !o)}
      >
        <span>
          <small>Contents</small> {sectionLabel(doc.section)} / {doc.title}
        </span>
        <ChevronDown size={16} aria-hidden="true" />
      </button>

      <aside id="docs-nav" className={"sidebar" + (navOpen ? " open" : "")} aria-label="Documentation">
        {sections.map((s) => (
          <details key={`${doc.slug}-${s.id}`} className="side-group" open={s.id === doc.section}>
            <summary className="side-head">
              <ChevronRight size={14} aria-hidden="true" />
              {s.label}
            </summary>
            <div className="side-items">
              {s.docs.map((d) => (
                <NavLink key={d.slug} to={`/docs/${d.slug}`} className="side-link" end>
                  {d.title}
                </NavLink>
              ))}
            </div>
          </details>
        ))}
      </aside>

      <article className="doc">
        <p className="crumb">
          <Link to="/docs/overview">Docs</Link> / {sectionLabel(doc.section)} / <span>{doc.title}</span>
        </p>
        <div className="md" onClick={onBodyClick} dangerouslySetInnerHTML={{ __html: content.html }} />
        <nav className="pager" aria-label="Previous and next">
          {prev ? (
            <Link to={`/docs/${prev.slug}`} className="pager-prev">
              <small>Previous</small>
              <span><ChevronLeft size={16} aria-hidden="true" />{prev.title}</span>
            </Link>
          ) : (
            <span />
          )}
          {next ? (
            <Link to={`/docs/${next.slug}`} className="pager-next">
              <small>Next</small>
              <span>{next.title}<ChevronRight size={16} aria-hidden="true" /></span>
            </Link>
          ) : (
            <span />
          )}
        </nav>
      </article>

      {pageHeadings.length > 1 && (
        <aside className="toc" aria-label="On this page">
          <div className="side-head">On this page</div>
          {pageHeadings.map((h) => (
            <a
              key={h.id}
              href={`#${h.id}`}
              className={"toc-" + h.depth + (h.id === activeHeading ? " active" : "")}
              aria-current={h.id === activeHeading ? "location" : undefined}
            >
              {h.text}
            </a>
          ))}
        </aside>
      )}
    </div>
  );
}

// react-router's lazy-route convention: the module exports `Component`.
export const Component = DocPage;
