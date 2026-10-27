import { useEffect, useState } from "react";
import { Link, NavLink, Outlet, useLocation } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { Menu, Moon, Sun, X } from "lucide-react";
import { Logo } from "./Logo";
import { MwLogo } from "./MwLogo";
import { absolute, GITHUB, SITE_NAME } from "../site";

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
          <button className="menu-btn" aria-label={open ? "Close navigation" : "Open navigation"} aria-expanded={open} aria-controls="nav" onClick={() => setOpen((o) => !o)}>
            {open ? <X size={20} aria-hidden="true" /> : <Menu size={20} aria-hidden="true" />}
          </button>
          <nav id="nav" className={"nav" + (open ? " open" : "")}>
            <NavLink to="/docs/getting-started">Get started</NavLink>
            <NavLink to="/docs/protocols/topics">Protocols</NavLink>
            <NavLink to="/docs/architecture">Architecture</NavLink>
            <NavLink to="/docs/modules/resources">Modules</NavLink>
            <NavLink to="/docs/mcp" className="nav-mcp">MCP</NavLink>
            <a href={GITHUB} target="_blank" rel="noreferrer">GitHub</a>
            <button className="theme-btn" onClick={toggleTheme} aria-label={`Switch to ${theme === "dark" ? "light" : "dark"} theme`} title={`Switch to ${theme === "dark" ? "light" : "dark"} theme`}>
              <Sun className="theme-icon theme-sun" size={19} aria-hidden="true" />
              <Moon className="theme-icon theme-moon" size={19} aria-hidden="true" />
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
            <MwLogo size={18} className="foot-logo" /> a <a href="https://mattediworks.com">Mattedi Works</a> project
          </span>
          <span>
            <a href={GITHUB}>Source on GitHub</a> · <Link to="/docs/mcp">MCP endpoint</Link> · MIT
          </span>
        </div>
      </footer>
    </>
  );
}
