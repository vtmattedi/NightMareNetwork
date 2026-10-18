import { Link, NavLink, useParams } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { ChevronLeft, ChevronRight } from "lucide-react";
import { Markdown } from "../components/Markdown";
import { docBySlug, neighbours, sections } from "../lib/docs";
import { NotFound } from "./NotFound";

export function DocPage() {
  const { a, b } = useParams();
  const slug = [a, b].filter(Boolean).join("/") || "overview";
  const doc = docBySlug(slug);
  if (!doc) return <NotFound />;
  const { prev, next } = neighbours(doc.slug);
  const pageHeadings = doc.headings.filter((h) => h.depth === 2);

  return (
    <div className="wrap docs">
      <Head>
        <title>{doc.title} · NightMare Network</title>
        <meta name="description" content={doc.description} />
        <meta property="og:title" content={`${doc.title} · NightMare Network`} />
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
          <Link to="/docs">Docs</Link> / {s(doc.section)} / <span>{doc.title}</span>
        </p>
        <Markdown source={doc.body} slug={doc.slug} />
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

function s(id: string) {
  const found = sections.find((x) => x.id === id);
  return found ? found.label : id;
}

// react-router's lazy-route convention: the module exports `Component`.
export const Component = DocPage;
