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

// Internal links become router links so navigation stays client-side; external
// ones open in a new tab. Anchors (#...) stay plain.
function A({ href = "", children, ...rest }: ComponentPropsWithoutRef<"a">) {
  if (href.startsWith("/")) {
    return (
      <Link to={href} {...rest}>
        {children}
      </Link>
    );
  }
  const external = /^https?:\/\//.test(href);
  return (
    <a href={href} {...(external ? { target: "_blank", rel: "noreferrer" } : {})} {...rest}>
      {children}
    </a>
  );
}

export function Markdown({ source }: { source: string }) {
  return (
    <div className="md">
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        rehypePlugins={[rehypeSlug, [rehypeHighlight, { languages, detect: false }]]}
        components={{ a: A }}
      >
        {source}
      </ReactMarkdown>
    </div>
  );
}
