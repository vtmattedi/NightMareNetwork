import { Link } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { Logo } from "../components/Logo";

const MCP_URL = "https://nightmare.mattediworks.com/mcp";

export function Home() {
  return (
    <>
      <Head>
        <title>NightMare Network · ESP32 devices that speak the same language</title>
        <meta
          name="description"
          content="NightMare Network is an ESP32 C++ framework and MQTT resource protocol for devices that expose, discover and use each other's state and capabilities."
        />
        <meta property="og:title" content="NightMare Network" />
      </Head>

      <section className="hero">
        <div className="wrap hero-inner">
          <Logo size={140} className="hero-logo" />
          <h1>
            ESP32 devices that speak <span className="accent">the same language</span>.
          </h1>
          <p className="lede">
            NightMare Network standardizes the plumbing that otherwise gets rebuilt in every
            networked firmware project: identity, MQTT addressing, retained state, Resources,
            commands, scheduling and device telemetry.
          </p>
          <p className="lede">
            <strong>Register once, participate automatically.</strong> Your application still owns
            the hardware and business logic; NightMare owns the repeated network behavior around it.
          </p>
          <div className="cta">
            <Link className="btn btn-primary" to="/docs/getting-started">
              Get started
            </Link>
            <Link className="btn" to="/docs/mcp">
              Connect the MCP
            </Link>
          </div>
          <pre className="hero-code">
            <code>{`lib_deps =
    https://github.com/vtmattedi/NightMareNetwork.git`}</code>
          </pre>
        </div>
      </section>

      <section className="wrap features">
        <Feature title="Resources are the application contract" to="/docs/modules/resources">
          Expose typed Values and Actions as <code>Managed</code> Resources, or bind{" "}
          <code>Remote</code> Resources implemented by another device. The framework handles
          manifests, retained state, subscriptions and reconnect.
        </Feature>

        <Feature title="Identity and presence stay separate" to="/docs/protocols/status-info">
          Retained <code>status</code> carries logical name, physical hardware signature and
          online state. Resource freshness comes from each Value&apos;s retained{" "}
          <code>/state</code>, not from device presence.
        </Feature>

        <Feature title="One command grammar, several transports" to="/docs/protocols/commands">
          The same command handler serves serial, the MQTT console, correlated MQTTP requests and
          persisted Scheduler command jobs.
        </Feature>

        <Feature title="Scheduling without application boilerplate" to="/docs/modules/scheduler">
          Wall or monotonic, one-shot or recurring, String command or callback. USER jobs stay
          separate from MANAGED framework/application jobs.
        </Feature>

        <Feature title="Local and remote MQTT are transport choices" to="/docs/modules/network">
          Devices normally share a Local MQTT cluster and can bridge toward Remote MQTT/backend
          services. Broker choice is independent of Resource ownership.
        </Feature>

        <Feature title="Docs and source share one MCP" to="/docs/mcp">
          The MCP server searches the same <code>docs/</code>, active <code>src/</code> and
          examples in the repository and stamps answers with the revision they came from.
        </Feature>
      </section>

      <section className="wrap wire">
        <h2>What goes on the wire</h2>
        <p>
          Device-scoped state is rooted under the logical device name. Descriptions and current
          state are retained; writes, Action invocations and commands are transient.
        </p>
        <pre className="code">
          <code>{`bedroom-ac/status                         {"name":"bedroom-ac","hardware":"Esp32-nm-6ca172e0","online":true}   retained
bedroom-ac/info                           {"identity":{...},"hardware":{...},"build":{...},"boot":{...}}        retained
bedroom-ac/telemetry/system               {"uptime_ms":3612000,"free_heap_bytes":...}                            retained

bedroom-ac/resources                      {"version":2,"resources":[...]}                                        retained
bedroom-ac/resources/temperature/state    23.44                                                                 retained
bedroom-ac/resources/power/state          true                                                                  retained

→ bedroom-ac/resources/power/set          false
→ bedroom-ac/resources/identify/invoke

→ bedroom-ac/console/controlled/req-42/in   INFO SYSTEM
← bedroom-ac/console/controlled/req-42/out  {"uptime_ms":3612000,"cpu_mhz":160,...}`}</code>
        </pre>
        <p>
          <Link to="/docs/protocols/topics">Topics</Link> ·{" "}
          <Link to="/docs/protocols/resources">Resources</Link> ·{" "}
          <Link to="/docs/protocols/mqttp">MQTTP</Link> ·{" "}
          <Link to="/docs/protocols/commands">Commands</Link>
        </p>
      </section>

      <section className="wrap arch">
        <h2>One repository, three consumers</h2>
        <pre className="code diagram">
          <code>{`                 NightMare Network repository
                           │
                ┌──────────┴──────────┐
                │                     │
             src/  (C++)          docs/  (Markdown)
                │                     │
          PlatformIO            ┌─────┴─────┐
                │               ▼           ▼
             ESP32           Website       MCP
                                │           │
                              Humans        AI`}</code>
        </pre>
        <p>
          The library, documentation, website and MCP server live together. The website and MCP
          consume the same Markdown corpus, while the MCP can also inspect the active C++ source
          when exact implementation behavior matters.
        </p>
      </section>

      <section className="wrap mcp-cta">
        <div className="mcp-card">
          <h2>Let your assistant read the source</h2>
          <p>
            A read-only Streamable HTTP MCP endpoint provides documentation search, exact API
            lookup, source inspection, examples and revision information.
          </p>
          <pre className="code">
            <code>{`claude mcp add --transport http nightmare ${MCP_URL}`}</code>
          </pre>
          <Link className="btn btn-primary" to="/docs/mcp">
            MCP documentation
          </Link>
        </div>
      </section>
    </>
  );
}

function Feature({
  title,
  to,
  children,
}: {
  title: string;
  to: string;
  children: React.ReactNode;
}) {
  return (
    <Link to={to} className="feature">
      <h3>{title}</h3>
      <p>{children}</p>
    </Link>
  );
}
