import type { MouseEvent } from "react";
import { Link, NavLink, useLoaderData, useNavigate } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { ChevronLeft, ChevronRight } from "lucide-react";
import { docBySlug, neighbours, sectionLabel, sections } from "../lib/docs";
import type { DocContent } from "../lib/sections";
import { absolute, SITE_NAME } from "../site";
import { NotFound } from "./NotFound";

export function DocPage() {
  const content = useLoaderData() as DocContent | null;
  const navigate = useNavigate();
  const doc = content && docBySlug(content.slug);
  if (!content || !doc) return <NotFound />;
  const { prev, next } = neighbours(doc.slug);
  const pageHeadings = content.headings.filter((h) => h.depth === 2);
  const url = absolute(`/docs/${doc.slug}/`);

  // The body is prerendered HTML, so its internal links are plain anchors.
  // Route them through the router to keep navigation client-side.
  const onBodyClick = (e: MouseEvent<HTMLDivElement>) => {
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
    const a = (e.target as HTMLElement).closest("a");
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

      <aside className="sidebar" aria-label="Documentation">
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
            <a key={h.id + h.text} href={`#${h.id}`} className={"toc-" + h.depth}>
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
