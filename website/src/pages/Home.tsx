import { Link } from "react-router-dom";
import { Head } from "vite-react-ssg";
import { Logo } from "../components/Logo";

const MCP_URL = "https://nightmare.mattediworks.com/mcp";

export function Home() {
  return (
    <>
      <Head>
        <title>NightMare Network · Build the device, share the language</title>
        <meta
          name="description"
          content="NightMare Network gives custom ESP32 devices a quick start with shared MQTT, commands, configuration and structured Resources, while you keep control of the device."
        />
        <meta property="og:title" content="NightMare Network" />
      </Head>

      <section className="hero">
        <div className="wrap hero-inner">
          <Logo size={140} className="hero-logo" />
          <h1>
            Build the device. <span className="accent">Share the language.</span>
          </h1>
          <p className="lede">
            NightMare Network grew from helpers reused across projects into a practical starting
            point for custom ESP32 devices. Identity, MQTT, commands, configuration and Resources
            already have a place; your firmware decides what the device does.
          </p>
          <p className="lede">
            <strong>Declare it, then use it.</strong> Bind a Config or a sensor and its declaration
            appears in the appropriate manifest. Keep full control of the hardware and behavior.
          </p>
          <div className="cta">
            <Link className="btn btn-primary" to="/docs/getting-started">
              Get started
            </Link>
            <a className="btn" href="#story">
              How it grew
            </a>
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

      <section id="story" className="wrap story" aria-labelledby="story-title">
        <div className="story-intro">
          <p className="eyebrow">The project so far</p>
          <h2 id="story-title">From repeated helpers to a common device language</h2>
          <p>
            Each stage came from building devices and finding the same work in the next project.
            The result is a framework for the shared parts, with room for each device to be its own.
          </p>
        </div>
        <ol className="story-steps">
          <li>
            <span className="story-number">01</span>
            <div>
              <h3>Helpers became a conversation</h3>
              <p>
                Reused firmware helpers grew into MQTT addressing, a common command resolver and
                failover. Projects could talk to their microcontrollers through one familiar path.
              </p>
            </div>
          </li>
          <li>
            <span className="story-number">02</span>
            <div>
              <h3>MQTTP added replies</h3>
              <p>
                A request and response model over MQTT gave each caller a correlated reply topic.
                It still uses the same command grammar and project resolver.
              </p>
            </div>
          </li>
          <li>
            <span className="story-number">03</span>
            <div>
              <h3>Capabilities became explicit</h3>
              <p>
                Today, typed Resources describe Values, Actions and Events. Config declarations
                have their own manifest, and Remote Resources bind a stable local name to a source.
              </p>
            </div>
          </li>
        </ol>
        <Link to="/docs/overview">Read the full overview →</Link>
      </section>

      <section className="wrap fit" aria-labelledby="fit-title">
        <div>
          <p className="eyebrow">The intended fit</p>
          <h2 id="fit-title">A quick start for the device you actually want to build</h2>
          <p>
            NightMare handles the repeatable network work. You choose the sensors, the actions,
            the event payloads, the hardware, and the rules that make this device useful.
          </p>
          <p>
            Think of Watson, a custom acoustic detector: its detection logic belongs to that
            project, while its configuration and network conversation can use common tools.
          </p>
        </div>
        <div className="fit-note">
          <h3>Practical scope</h3>
          <p>
            This is a toolkit for specific, tailored devices. It does not try to optimize every
            subsystem, become a home automation platform, or prescribe a mass production product.
          </p>
          <p>
            The library builds under ESP-IDF or Arduino. OTA alone remains an optional
            Arduino-only module.
          </p>
        </div>
      </section>

      <section className="wrap features">
        <Feature title="Resources are the application contract" to="/docs/modules/resources">
          Expose typed Values, Actions and Events as <code>Managed</code> Resources, or bind{" "}
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
          Wall or monotonic, one-shot or recurring, <code>std::string</code> command or callback. USER jobs stay
          separate from MANAGED framework/application jobs.
        </Feature>

        <Feature title="Local and remote MQTT are transport choices" to="/docs/modules/network">
          Devices can use a Local MQTT cluster or Remote MQTT/backend services. Broker policy and
          transport failover stay independent of Resource ownership.
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
          <code>{`bedroom-ac/status                          {"name":"bedroom-ac","hardware":"...","timezone":"UTC0","online":true}  retained
bedroom-ac/manifest/msgpack                <MessagePack Resource manifest>                            retained
bedroom-ac/manifest/consume/msgpack        <MessagePack consume manifest>                             retained
bedroom-ac/resource/temperature/state     23.44                                                      retained

→ bedroom-ac/resource/power/set            false
→ bedroom-ac/resource/identify/invoke
→ bedroom-ac/console/controlled/req-42/in   INFO SYSTEM
← bedroom-ac/console/controlled/req-42/out  {"uptime_ms":3612000,"cpu_mhz":160,...}

CONFIG MANIFEST → Base64 of the MessagePack Config declaration manifest`}</code>
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
