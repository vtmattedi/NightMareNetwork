import { useEffect, useRef } from "react";
import { Link } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { ArrowRight, Bot, CalendarClock, Radio, Share2, Terminal, type LucideIcon } from "lucide-react";
import { Logo } from "../components/Logo";
import { siteInfo } from "virtual:site-info";
import { absolute, GITHUB, LIB_DEPS, MCP_URL, SITE_NAME, VERSION } from "../site";

// The homepage follows one device, desk-lamp, from the code to the wire.
// The sketch is examples/basic trimmed to what the story needs; keep the two
// in step.
const SKETCH = `#include <Arduino.h>
#include <NightMare.h>

ManagedSensor<uint32_t> uptime("uptime_s");
ManagedState<uint8_t>   brightness("brightness");
ManagedAction           identify("identify");

bool onBrightness(ManagedState<uint8_t> &, const uint8_t &level) {
    return level <= 100;          // false rejects the write
}

ActionResult onIdentify(ManagedAction &, const std::string &) {
    blinkLed();                   // your hardware, your code
    return {true, "OK"};
}

void publishUptime() { uptime.setValue(millis() / 1000); }

void setup() {
    brightness.onWrite = onBrightness;
    identify.onInvoke  = onIdentify;
    gScheduler.everyMonotonic("app.uptime", publishUptime, 1000);
    startNightMareESP();
}

void loop() { tickNightMareESP(); }`;

const WIRE: { topic: string; payload: string; note: string; dir?: "in" }[] = [
  { topic: "desk-lamp/status", payload: `{"name":"desk-lamp","online":true,…}`, note: "retained" },
  { topic: "desk-lamp/manifest/msgpack", payload: "uptime_s · brightness · identify", note: "retained" },
  { topic: "desk-lamp/resource/uptime_s/state", payload: "3612", note: "retained" },
  { topic: "desk-lamp/resource/brightness/state", payload: "40", note: "retained" },
  { topic: "desk-lamp/resource/brightness/set", payload: "80", note: "onWrite → accepted", dir: "in" },
  { topic: "desk-lamp/resource/identify/invoke", payload: "", note: "onInvoke → blink", dir: "in" },
];

// Real firmware built on the library. Links only where the repository is public.
const BUILT_WITH: { name: string; what: string; how: string; href?: string; footnote?: string }[] = [
  {
    name: "Adler-IR",
    what: "Infrared air-conditioner controller for ESP32, C3 and C6 boards.",
    how: "Exposes ir:send and ir:recv, a temperature sensor and an RGB state, and consumes the AC's fan, compressor and beep from other devices as Remote Resources bound at runtime.",
    href: "https://github.com/vtmattedi/Adler-IR",
  },
  {
    name: "Sherlock",
    what: "Room sensor and light on ESP32.",
    how: "Temperature, humidity and board voltage as Managed Sensors; LED power and colour as writable State.",
    href: "https://github.com/vtmattedi/Sherlock",
  },
  {
    name: "NightMare Gateway",
    what: "ESP32-S3 bridge between ESP-NOW devices and MQTT.",
    how: "Pairs ESP-NOW's speed, under 10 ms per hop*, with MQTT's reach. Itself a NightMare device, with an identity, Configs, Resources and a console, so it is managed like everything it carries.",
    href: "https://github.com/vtmattedi/NightmareGateway",
    footnote: "* Device to gateway over ESP-NOW; 5–8 ms in our testing. Radio conditions vary.",
  },
  {
    name: "Watson",
    what: "Acoustic detector.",
    how: "Its detection logic is its own; the beeps it hears are NightMare Events that other devices, like Adler-IR, can bind to.",
  },
  {
    name: "MwNightmareSystem",
    what: "Home dashboard and history service.",
    how: "A backend that reads manifests, sends writes and Actions, and stores sensor history: a consumer of the protocol, not a part of it.",
  },
];

/** The latest library and documentation changes, without merges and site-only work. */
const recentChanges = siteInfo.changes
  .filter((c) => /^(feat|fix|perf|refactor)(\([^)]*\))?!?:/.test(c.subject) && !/^\w+\(website\)/.test(c.subject))
  .slice(0, 5);

const day = (iso: string) =>
  new Date(iso).toLocaleDateString("en-GB", { day: "numeric", month: "short", year: "numeric", timeZone: "UTC" });

export function Home() {
  const pageRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const page = pageRef.current;
    if (!page || !window.IntersectionObserver || window.matchMedia("(prefers-reduced-motion: reduce)").matches) return;

    const items = page.querySelectorAll<HTMLElement>("[data-reveal]");
    const observer = new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (entry.isIntersecting) {
          entry.target.classList.add("is-visible");
          observer.unobserve(entry.target);
        }
      }
    }, { threshold: 0.08, rootMargin: "0px 0px -24px 0px" });

    page.classList.add("motion-ready");
    items.forEach((item) => observer.observe(item));
    return () => observer.disconnect();
  }, []);

  return (
    <>
      <Head>
        <title>{`${SITE_NAME} · Declare it once, your ESP32 joins the network`}</title>
        <meta
          name="description"
          content="An ESP32 framework and MQTT protocol: declare a sensor, a setting or an action once and NightMare publishes, routes, retains and reconnects it. Your firmware keeps the hardware."
        />
        <link rel="canonical" href={absolute("/")} />
        <meta property="og:type" content="website" />
        <meta property="og:url" content={absolute("/")} />
        <meta property="og:title" content={SITE_NAME} />
      </Head>

      <div className="home-page" ref={pageRef}>
        {/* 1. The problem and the promise. */}
        <section className="hero">
          <div className="wrap hero-inner">
            <Logo size={96} className="hero-logo" />
            <p className="hero-status">
              ESP32 · MQTT · ESP-NOW · <a href={`${GITHUB}/blob/main/library.json`}>v{VERSION}</a>
            </p>
            <h1>
              Declare it once. <span className="accent">Your device joins the network.</span>
            </h1>
            <p className="lede">
              Every networked ESP32 project rebuilds the same plumbing: an identity, MQTT topics,
              retained state, reconnects, remote commands, schedules. NightMare does that part.
              You declare what the device offers; your firmware keeps the hardware.
            </p>
            <p className="thesis">Register once, participate automatically.</p>
            <div className="cta">
              <Link className="btn btn-primary" to="/docs/getting-started">
                Build your first device
              </Link>
              <a className="btn" href="#aha">
                See what it does
              </a>
            </div>
            <pre className="hero-code">
              <code>{LIB_DEPS}</code>
            </pre>
          </div>
        </section>

        {/* 2. The aha: code on one side, the wire it produces on the other. */}
        <section id="aha" className="wrap aha" aria-labelledby="aha-title">
          <div className="section-intro" data-reveal>
            <p className="eyebrow">One device, start to finish</p>
            <h2 id="aha-title">You write the code. The network sees the topics.</h2>
            <p>
              Three declarations and two handlers. No topic strings, no subscribe calls, no reconnect
              logic. After <code>ADOPT desk-lamp</code> on its console, this is what any other device,
              dashboard or script finds on the broker.
            </p>
          </div>
          <div className="aha-grid" data-reveal>
            <figure className="aha-pane">
              <figcaption>src/main.cpp</figcaption>
              <pre className="code">
                <code>{SKETCH}</code>
              </pre>
            </figure>
            <figure className="aha-pane">
              <figcaption>MQTT</figcaption>
              <ul className="wire-list">
                {WIRE.map((w) => (
                  <li key={w.topic} className={w.dir === "in" ? "wire-in" : undefined}>
                    <code className="wire-topic">
                      {w.dir === "in" ? "→ " : ""}
                      {w.topic}
                    </code>
                    {w.payload && <code className="wire-payload">{w.payload}</code>}
                    <span className="wire-note">{w.note}</span>
                  </li>
                ))}
              </ul>
              <p className="aha-foot">
                A write of <code>180</code> is rejected by <code>onBrightness</code>, so the retained state stays
                at the last accepted value. The device reboots, the broker drops, the Wi-Fi comes back: the
                manifests and state are re-announced without a line of your code.
              </p>
            </figure>
          </div>
          <p className="aha-links">
            <Link to="/docs/getting-started">Getting started</Link> ·{" "}
            <Link to="/docs/protocols/topics">Every topic</Link> ·{" "}
            <Link to="/docs/protocols/resources">Resource protocol</Link> ·{" "}
            <a href={`${GITHUB}/tree/main/examples/basic`}>examples/basic</a>
          </p>
        </section>

        {/* 3. The reassurance: where the line between framework and firmware sits. */}
        <section className="wrap split" aria-labelledby="split-title" data-reveal>
          <div className="section-intro">
            <p className="eyebrow">Who owns what</p>
            <h2 id="split-title">NightMare takes the plumbing. The device stays yours.</h2>
          </div>
          <div className="split-grid">
            <div className="split-col">
              <h3>NightMare handles</h3>
              <ul>
                <li>Device identity, naming and renaming, with cleanup of the old name's retained data</li>
                <li>Topics, retained manifests and state, subscriptions, re-announcing after reconnect</li>
                <li>Routing writes, Action calls and commands to your handlers</li>
                <li>Local or remote MQTT, ESP-NOW through a gateway, and failover between them</li>
                <li>Scheduling, persisted settings, telemetry and time sync</li>
              </ul>
            </div>
            <div className="split-col split-yours">
              <h3>Your firmware decides</h3>
              <ul>
                <li>The hardware: pins, drivers, sensors, timing</li>
                <li>What each Value means and when it changes</li>
                <li>Whether a write is accepted, and what an Action does</li>
                <li>Which devices it listens to, and what it does with what it hears</li>
                <li>Everything the device is actually for</li>
              </ul>
            </div>
          </div>
          <Link to="/docs/responsibilities" className="text-link">
            The full split <ArrowRight size={17} aria-hidden="true" />
          </Link>
        </section>

        {/* 4. What you get, in plain words. */}
        <section className="wrap features" aria-label="What you get">
          <Feature icon={Share2} title="Share state with every device" to="/docs/modules/resources">
            A Value's latest reading is retained, so a device that boots later sees it immediately. Another
            device consumes it by name with a <code>Remote</code> Resource and can switch the source at runtime.
          </Feature>
          <Feature icon={Radio} title="Call other devices and react to them" to="/docs/protocols/resources">
            Actions are typed remote calls with a result; Events are things that happened. Both are routed for
            you, in both directions.
          </Feature>
          <Feature icon={Terminal} title="One command language everywhere" to="/docs/protocols/commands">
            The same commands work over serial, the MQTT console, correlated MQTTP requests and scheduled jobs.
            Rename a device, change a setting or inspect it from any of them.
          </Feature>
          <Feature icon={CalendarClock} title="Schedules that survive reboots" to="/docs/modules/scheduler">
            Wall-clock or monotonic, one-shot or recurring, callbacks or commands; command jobs can be
            persisted and edited remotely.
          </Feature>
        </section>

        {/* 5. Proof: real firmware built on it. */}
        <section className="wrap built" aria-labelledby="built-title">
          <div className="section-intro" data-reveal>
            <p className="eyebrow">Built with NightMare</p>
            <h2 id="built-title">Real devices, each its own project</h2>
            <p>
              NightMare grew out of these projects: the same helpers kept reappearing in each one until they
              became a framework. <Link to="/docs/overview#how-it-grew">How it grew</Link>
            </p>
          </div>
          <ul className="built-grid">
            {BUILT_WITH.map((b) => (
              <li key={b.name} className="built-card" data-reveal>
                <h3>{b.href ? <a href={b.href}>{b.name}</a> : b.name}</h3>
                <p className="built-what">{b.what}</p>
                <p>{b.how}</p>
                {b.footnote && <p className="built-note">{b.footnote}</p>}
              </li>
            ))}
          </ul>
        </section>

        {/* 6. The differentiator. */}
        <section className="wrap mcp-cta">
          <div className="mcp-card" data-reveal>
            <Bot size={26} className="feature-icon" aria-hidden="true" />
            <h2>Your AI assistant can read the exact source</h2>
            <p>
              A public, read-only MCP endpoint lets Claude or any MCP client search these docs, look up the exact
              API, read the active C++ source and examples, and say which revision it answered from. Ask it to
              write a device; it checks the real headers instead of guessing.
            </p>
            <pre className="code">
              <code>{`claude mcp add --transport http nightmare ${MCP_URL}`}</code>
            </pre>
            <Link className="btn btn-primary" to="/docs/mcp">
              Connect the MCP
            </Link>
          </div>
        </section>

        {/* 7. Where it is. */}
        <section className="wrap status" aria-labelledby="status-title" data-reveal>
          <div className="status-grid">
            <div>
              <p className="eyebrow">Where it is</p>
              <h2 id="status-title">v{VERSION}</h2>
              <p>
                The Resource model, the topic layout and the command grammar are the 1.0 contract. Alpha means
                the release process is new: pin the tag above, and read the{" "}
                <Link to="/docs/architecture/known-gaps">known gaps</Link> for what is deliberately not done yet.
              </p>
              <p>
                It is built and used continuously in the devices above, so it changes often. Every page here
                says when it last changed.
              </p>
            </div>
            {recentChanges.length > 0 && (
              <div>
                <h3>Recent changes</h3>
                <ul className="changes">
                  {recentChanges.map((c) => (
                    <li key={c.sha}>
                      <time dateTime={c.date}>{day(c.date)}</time>
                      <a href={`${GITHUB}/commit/${c.sha}`}>{c.subject}</a>
                    </li>
                  ))}
                </ul>
                <a href={`${GITHUB}/commits/main`} className="text-link">
                  All commits <ArrowRight size={17} aria-hidden="true" />
                </a>
              </div>
            )}
          </div>
        </section>
      </div>
    </>
  );
}

function Feature({
  icon: Icon,
  title,
  to,
  children,
}: {
  icon: LucideIcon;
  title: string;
  to: string;
  children: React.ReactNode;
}) {
  return (
    <Link to={to} className="feature" data-reveal>
      <Icon size={23} strokeWidth={1.8} className="feature-icon" aria-hidden="true" />
      <h3>{title}</h3>
      <p>{children}</p>
    </Link>
  );
}
