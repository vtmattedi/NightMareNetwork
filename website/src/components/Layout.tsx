import { useEffect, useState } from "react";
import { Link, Outlet, ScrollRestoration, useLocation } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { Menu, Moon, Sun, X } from "lucide-react";
import { Logo } from "./Logo";
import { MwLogo } from "./MwLogo";
import { absolute, GITHUB, SITE_NAME, VERSION } from "../site";
import { siteInfo } from "virtual:site-info";
import { Search } from "./Search";
import { docBySlug } from "../lib/docs";
import type { SectionId } from "../lib/sections";

// Each top-level link stands for a docs section and is highlighted on every
// page of that section, not only on the page it points to.
const NAV: { label: string; to: string; section: SectionId; className?: string }[] = [
  { label: "Get started", to: "/docs/getting-started", section: "getting-started" },
  { label: "Protocols", to: "/docs/protocols/topics", section: "protocols" },
  { label: "Architecture", to: "/docs/architecture", section: "architecture" },
  { label: "Modules", to: "/docs/modules/resources", section: "modules" },
  { label: "MCP", to: "/docs/mcp", section: "mcp", className: "nav-mcp" },
];

type Theme = "dark" | "light";

function useTheme(): [Theme, () => void] {
  // Server render and first client render agree on "dark"; the inline script in
  // index.html has already set data-theme, and this syncs state to it after mount.
  const [theme, setTheme] = useState<Theme>("dark");
  useEffect(() => {
    const t = document.documentElement.getAttribute("data-theme");
    if (t === "light" || t === "dark") setTheme(t);
    // Until the visitor picks a theme, keep following the system setting.
    const media = window.matchMedia("(prefers-color-scheme: light)");
    const follow = (e: MediaQueryListEvent) => {
      try {
        if (localStorage.getItem("nm-theme")) return;
      } catch {
        /* private mode */
      }
      const next: Theme = e.matches ? "light" : "dark";
      document.documentElement.setAttribute("data-theme", next);
      setTheme(next);
    };
    media.addEventListener("change", follow);
    return () => media.removeEventListener("change", follow);
  }, []);
  const toggle = () => {
    const next: Theme = theme === "dark" ? "light" : "dark";
    setTheme(next);
    document.documentElement.setAttribute("data-theme", next);
    try {
      localStorage.setItem("nm-theme", next);
    } catch {
      /* private mode */
    }
  };
  return [theme, toggle];
}

export function Layout() {
  const [theme, toggleTheme] = useTheme();
  const [open, setOpen] = useState(false);
  const location = useLocation();
  const slug = location.pathname.replace(/^\/docs\/?/, "").replace(/\/$/, "");
  const currentSection = location.pathname.startsWith("/docs") ? docBySlug(slug || "overview")?.section : undefined;
  useEffect(() => setOpen(false), [location.pathname]);

  return (
    <>
      <Head>
        <meta name="description" content="NightMare Network: practical network tools for custom ESP32 devices, with shared MQTT, structured Resources, documentation and a read-only MCP server." />
        <meta property="og:site_name" content={SITE_NAME} />
        <meta property="og:image" content={absolute("/og-image.png")} />
        <meta name="twitter:card" content="summary_large_image" />
      </Head>
      <a className="skip" href="#main">Skip to content</a>
      <header className="top">
        <div className="wrap top-inner">
          <Link to="/" className="brand" aria-label="NightMare Network home">
            <Logo size={30} />
            <span className="brand-name">
              NightMare<span className="brand-dim"> Network</span>
            </span>
          </Link>
          <a
            className="version"
            href={`${GITHUB}/tree/${siteInfo.revision ?? "main"}`}
            title={siteInfo.revision ? `Built from ${siteInfo.revision}` : undefined}
          >
            v{VERSION}
          </a>
          <button className="menu-btn" aria-label={open ? "Close navigation" : "Open navigation"} aria-expanded={open} aria-controls="nav" onClick={() => setOpen((o) => !o)}>
            {open ? <X size={20} aria-hidden="true" /> : <Menu size={20} aria-hidden="true" />}
          </button>
          <nav id="nav" className={"nav" + (open ? " open" : "")}>
            {NAV.map((item) => {
              const active = item.section === currentSection;
              return (
                <Link key={item.to} to={item.to} className={[item.className, active && "active"].filter(Boolean).join(" ") || undefined} aria-current={active ? "true" : undefined}>
                  {item.label}
                </Link>
              );
            })}
            <a href={GITHUB} target="_blank" rel="noreferrer">GitHub</a>
            <button className="theme-btn" onClick={toggleTheme} aria-label={`Switch to ${theme === "dark" ? "light" : "dark"} theme`} title={`Switch to ${theme === "dark" ? "light" : "dark"} theme`}>
              <Sun className="theme-icon theme-sun" size={19} aria-hidden="true" />
              <Moon className="theme-icon theme-moon" size={19} aria-hidden="true" />
            </button>
          </nav>
          <Search />
        </div>
      </header>
      <main id="main">
        <Outlet />
        {/* New pages start at the top (or at their #fragment); back/forward restores the position. */}
        <ScrollRestoration />
      </main>
      <footer className="foot">
        <div className="wrap foot-inner">
          <span>
            <MwLogo size={18} className="foot-logo" /> a <a href="https://mattediworks.com">Mattedi Works</a> project
          </span>
          <span>
            <a href={GITHUB}>Source on GitHub</a> · <Link to="/docs/mcp">MCP endpoint</Link> · MIT
            {siteInfo.revision && (
              <>
                {" "}· v{VERSION} built from <a href={`${GITHUB}/commit/${siteInfo.revision}`}><code>{siteInfo.revision}</code></a>
              </>
            )}
          </span>
        </div>
      </footer>
    </>
  );
}
