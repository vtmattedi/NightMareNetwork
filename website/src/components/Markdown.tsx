import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import rehypeSlug from "rehype-slug";
import rehypeHighlight from "rehype-highlight";
import { Link } from "react-router-dom";
import type { ComponentPropsWithoutRef } from "react";
// Only the grammars the documentation uses. The default set is ~37 languages
// and most of the site's JavaScript by weight.
import cpp from "highlight.js/lib/languages/cpp";
import ini from "highlight.js/lib/languages/ini";
import json from "highlight.js/lib/languages/json";
import bash from "highlight.js/lib/languages/bash";
import yaml from "highlight.js/lib/languages/yaml";
import plaintext from "highlight.js/lib/languages/plaintext";

const languages = { cpp, c: cpp, ini, json, bash, sh: bash, shell: bash, yaml, yml: yaml, text: plaintext, plaintext };

// Markdown sources use paths relative to docs/, including links ending in .md.
// Translate those source paths to the site's canonical routes.
export function docHref(href: string, slug: string): string {
  if (!/\.md(?:#.*)?$/i.test(href) || /^(?:[a-z][a-z\d+.-]*:|\/\/)/i.test(href)) return href;
  const [sourcePath, fragment = ""] = href.split("#", 2);
  const parts = sourcePath.startsWith("/") ? [] : slug.split("/").slice(0, -1);
  for (const part of sourcePath.replace(/^\/docs\//, "").replace(/^\//, "").replace(/\.md$/i, "").split("/")) {
    if (part === "..") parts.pop();
    else if (part && part !== ".") parts.push(part);
  }
  return `/docs/${parts.join("/")}${fragment ? `#${fragment}` : ""}`;
}

// Internal links become router links so navigation stays client-side; external
// ones open in a new tab. Anchors (#...) stay plain.
function A({ href = "", children, slug, ...rest }: ComponentPropsWithoutRef<"a"> & { slug: string }) {
  const target = docHref(href, slug);
  if (target.startsWith("/")) {
    return (
      <Link to={target} {...rest}>
        {children}
      </Link>
    );
  }
  const external = /^https?:\/\//.test(target);
  return (
    <a href={target} {...(external ? { target: "_blank", rel: "noreferrer" } : {})} {...rest}>
      {children}
    </a>
  );
}

export function Markdown({ source, slug }: { source: string; slug: string }) {
  return (
    <div className="md">
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        rehypePlugins={[rehypeSlug, [rehypeHighlight, { languages, detect: false }]]}
        components={{ a: (props) => <A {...props} slug={slug} /> }}
      >
        {source}
      </ReactMarkdown>
    </div>
  );
}
