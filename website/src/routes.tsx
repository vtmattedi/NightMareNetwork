import type { RouteRecord } from "vite-react-ssg";
import { Layout } from "./components/Layout";
import { Home } from "./pages/Home";
import { NotFound } from "./pages/NotFound";
import { nestedSlugs, topLevelSlugs } from "./lib/docs-index";

// The docs page is a lazy route: it carries react-markdown, the syntax
// grammars and the whole markdown corpus, and the landing page needs none of
// that. The slug lists come from a content-free glob so this file stays light.
const docs = () => import("./pages/DocPage");

export const routes: RouteRecord[] = [
  {
    path: "/",
    element: <Layout />,
    entry: "src/components/Layout.tsx",
    children: [
      { index: true, element: <Home /> },
      { path: "docs", lazy: docs },
      {
        path: "docs/:a",
        lazy: docs,
        getStaticPaths: () => topLevelSlugs.map((s) => `docs/${s}`),
      },
      {
        path: "docs/:a/:b",
        lazy: docs,
        getStaticPaths: () => nestedSlugs.map((s) => `docs/${s}`),
      },
      { path: "*", element: <NotFound /> },
    ],
  },
];
