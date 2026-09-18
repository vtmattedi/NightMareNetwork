import { useEffect, useState } from "react";
import { Link, NavLink, Outlet, useLocation } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { Logo } from "./Logo";

const GITHUB = "https://github.com/vtmattedi/NightMareNetwork";

type Theme = "dark" | "light";

function useTheme(): [Theme, () => void] {
  // Server render and first client render agree on "dark"; the inline script in
  // index.html has already set data-theme, and this syncs state to it after mount.
  const [theme, setTheme] = useState<Theme>("dark");
  useEffect(() => {
    const t = document.documentElement.getAttribute("data-theme");
    if (t === "light" || t === "dark") setTheme(t);
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
  useEffect(() => setOpen(false), [location.pathname]);

  return (
    <>
      <Head>
        <meta name="description" content="NightMare Network: a C++ library and a set of conventions for ESP32 devices on a home MQTT network, with an MCP server for AI assistants." />
        <meta property="og:site_name" content="NightMare Network" />
        <meta property="og:image" content="/og-image.png" />
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
          <button className="menu-btn" aria-expanded={open} aria-controls="nav" onClick={() => setOpen((o) => !o)}>
            Menu
          </button>
          <nav id="nav" className={"nav" + (open ? " open" : "")}>
            <NavLink to="/docs/getting-started">Get started</NavLink>
            <NavLink to="/docs/protocols/topics">Protocols</NavLink>
            <NavLink to="/docs/architecture">Architecture</NavLink>
            <NavLink to="/docs/modules/mqtt">Modules</NavLink>
            <NavLink to="/docs/mcp" className="nav-mcp">MCP</NavLink>
            <a href={GITHUB} target="_blank" rel="noreferrer">GitHub</a>
            <button className="theme-btn" onClick={toggleTheme} aria-label={`Switch to ${theme === "dark" ? "light" : "dark"} theme`}>
              {theme === "dark" ? "Light" : "Dark"}
            </button>
          </nav>
        </div>
      </header>
      <main id="main">
        <Outlet />
      </main>
      <footer className="foot">
        <div className="wrap foot-inner">
          <span>
            <Logo size={18} className="foot-logo" /> a <a href="https://mattediworks.com">Mattedi Works</a> project
          </span>
          <span>
            <a href={GITHUB}>Source on GitHub</a> · <Link to="/docs/mcp">MCP endpoint</Link> · MIT
          </span>
        </div>
      </footer>
    </>
  );
}
