import { Link } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { Logo } from "../components/Logo";

const MCP_URL = "https://nightmare.mattediworks.com/mcp";

export function Home() {
  return (
    <>
      <Head>
        <title>NightMare Network · ESP32 devices that speak the same language</title>
        <meta name="description" content="A C++ library and a set of conventions for ESP32 devices on a home MQTT network, with an MCP server so AI assistants answer from the source." />
        <meta property="og:title" content="NightMare Network" />
      </Head>

      <section className="hero">
        <div className="wrap hero-inner">
          <Logo size={140} className="hero-logo" />
          <h1>
            ESP32 devices that speak <span className="accent">the same language</span>.
          </h1>
          <p className="lede">
            NightMare Network is a C++ library and the conventions around it: one line in{" "}
            <code>platformio.ini</code> gives a device identity, a console, request/response over MQTT,
            timers, a scheduler and persistent config. The wall panel, the backend and an AI assistant
            then all talk to it the same way.
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
        <Feature title="Identity for free" to="/docs/protocols/telemetry">
          Retained <code>status</code>, a last-will, telemetry every 15 s. A device is discoverable
          the moment it joins the broker, and adoptable with one console command.
        </Feature>
        <Feature title="One grammar, three transports" to="/docs/protocols/commands">
          The same command line works over serial, the MQTT console and MQTTP request/response.
          Fourteen built-ins before you write one of your own.
        </Feature>
        <Feature title="Sensors the backend understands" to="/docs/protocols/sensors">
          Readings as one JSON object, a declaration the ingest parser reads, and network sensors so a
          controller can use a reading that lives on another device.
        </Feature>
        <Feature title="Controllers and Services" to="/docs/protocols/actuators-controllers">
          Device-side policy that survives the cloud going away, and client-side proxies built on{" "}
          <code>ServerVariable</code>: optimistic, asserted, rolled back if the device never agreed.
        </Feature>
        <Feature title="A scheduler, not a clock compare" to="/docs/modules/scheduler">
          Persisted wall-clock tasks that fire commands. <code>SCHEDULER LIST</code> shows an operator
          what will happen and when.
        </Feature>
        <Feature title="Board revisions that never get lost" to="/docs/architecture">
          An append-only pin registry per device: every hardware revision keeps its map forever, and
          unselected ones cost zero flash.
        </Feature>
      </section>

      <section className="wrap wire">
        <h2>What goes on the wire</h2>
        <p>
          Everything a device says is under its own name. Two consumers — the Dashboard and the
          backend — were read line by line to write down exactly what each expects.
        </p>
        <pre className="code">
          <code>{`Adler/status        online                                   retained
Adler/telemetry     {"System":{"Uptime":3612,"FreeHeap":18.2,...}}
Adler/sensors       {"temperature":23.44,"door":false}
Adler/state         {"AcState":1,"Temp":24,"Settemp":23.5,"CurrTemp":23.44,...}

→ Adler/console/in                         SETTEMP 24
← Adler/console/out                        {"Temp":24}

→ Adler/console/controlled/9f1c/in         HARDWAREINFO
← Adler/console/controlled/9f1c/out        {"ChipModel":"ESP32-C3",...}`}</code>
        </pre>
        <p>
          <Link to="/docs/protocols/topics">Topics</Link> · <Link to="/docs/protocols/mqttp">MQTTP</Link> ·{" "}
          <Link to="/docs/protocols/commands">Commands</Link> · <Link to="/docs/protocols/sensors">Sensors</Link>
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
          The library, its documentation, this site and the MCP server live in one repository. A
          change to a module and the paragraph describing it land in the same commit, and the AI reads
          the same paragraph you do — at the revision it came from.
        </p>
      </section>

      <section className="wrap mcp-cta">
        <div className="mcp-card">
          <h2>Let your assistant read the source</h2>
          <p>
            A Streamable HTTP MCP endpoint with search over the docs, <code>get_api</code> for any
            declaration, and the examples. No accounts, no keys.
          </p>
          <pre className="code">
            <code>{`claude mcp add --transport http nightmare ${MCP_URL}`}</code>
          </pre>
          <Link className="btn btn-primary" to="/docs/mcp">
            Setup for Claude Code, Cursor, VS Code and Claude Desktop
          </Link>
        </div>
      </section>
    </>
  );
}

function Feature({ title, to, children }: { title: string; to: string; children: React.ReactNode }) {
  return (
    <Link to={to} className="feature">
      <h3>{title}</h3>
      <p>{children}</p>
    </Link>
  );
}
