import type { RouteRecord } from "vite-react-ssg";
import { Layout } from "./components/Layout";
import { Home } from "./pages/Home";
import { NotFound } from "./pages/NotFound";
import { docLoader } from "./lib/doc-loader";
import { nestedSlugs, topLevelSlugs } from "./lib/docs";

// The docs page is a lazy route so the landing page doesn't carry it. Its
// content arrives through docLoader, never as part of a JavaScript chunk.
const docs = () => import("./pages/DocPage");

export const routes: RouteRecord[] = [
  {
    path: "/",
    element: <Layout />,
    entry: "src/components/Layout.tsx",
    children: [
      { index: true, element: <Home /> },
      // /docs/ is an alias of the overview; nginx redirects it, and the page
      // points its canonical link at /docs/overview/.
      { path: "docs", lazy: docs, loader: docLoader },
      {
        path: "docs/:a",
        lazy: docs,
        loader: docLoader,
        getStaticPaths: () => topLevelSlugs.map((s) => `docs/${s}`),
      },
      {
        path: "docs/:a/:b",
        lazy: docs,
        loader: docLoader,
        getStaticPaths: () => nestedSlugs.map((s) => `docs/${s}`),
      },
      // Prerendered as /404/index.html, which nginx serves with a real 404
      // status for any path that has no page. The catch-all renders the same
      // markup, so the client hydrates it cleanly.
      { path: "404", element: <NotFound /> },
      { path: "*", element: <NotFound /> },
    ],
  },
];
