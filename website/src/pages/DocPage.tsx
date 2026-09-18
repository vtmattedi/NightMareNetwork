import { Link, NavLink, useParams } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { Markdown } from "../components/Markdown";
import { docBySlug, neighbours, sections } from "../lib/docs";
import { NotFound } from "./NotFound";

export function DocPage() {
  const { a, b } = useParams();
  const slug = [a, b].filter(Boolean).join("/") || "overview";
  const doc = docBySlug(slug);
  if (!doc) return <NotFound />;
  const { prev, next } = neighbours(doc.slug);

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
          <div key={s.id} className="side-group">
            <div className="side-head">{s.label}</div>
            {s.docs.map((d) => (
              <NavLink key={d.slug} to={`/docs/${d.slug}`} className="side-link" end>
                {d.title}
              </NavLink>
            ))}
          </div>
        ))}
      </aside>

      <article className="doc">
        <p className="crumb">
          <Link to="/docs">Docs</Link> / {s(doc.section)} / <span>{doc.title}</span>
        </p>
        <Markdown source={doc.body} />
        <nav className="pager" aria-label="Previous and next">
          {prev ? (
            <Link to={`/docs/${prev.slug}`} className="pager-prev">
              <small>Previous</small>
              {prev.title}
            </Link>
          ) : (
            <span />
          )}
          {next ? (
            <Link to={`/docs/${next.slug}`} className="pager-next">
              <small>Next</small>
              {next.title}
            </Link>
          ) : (
            <span />
          )}
        </nav>
      </article>

      {doc.headings.length > 2 && (
        <aside className="toc" aria-label="On this page">
          <div className="side-head">On this page</div>
          {doc.headings.map((h) => (
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
