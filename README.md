# NightMare Network

NightMare Network is an ESP32 C++ framework and MQTT resource protocol for devices that need to expose, discover, and use each other's state and capabilities without rebuilding the same network plumbing in every firmware project.

Its guiding idea is:

> **Register once, participate automatically.**

An application declares the Resources it owns or consumes, binds its handlers, and starts NightMare. The framework takes care of the repeated infrastructure around identity, MQTT addressing, retained state, discovery, scheduling, command routing, telemetry, reconnect behavior, and common ESP32 lifecycle wiring.

NightMare does **not** try to hide embedded development. The application still owns its hardware drivers, sensor acquisition, actuator behavior, and business logic.

## Start here

- [Overview](docs/overview.md) — motivation, philosophy, and the system model.
- [Core concepts](docs/concepts.md) — the vocabulary used throughout the project.
- [Responsibilities](docs/responsibilities.md) — what NightMare owns and what the application owns.
- [Architecture](docs/architecture.md) — how the pieces fit together at runtime.
- [Design decisions](docs/architecture/design-decisions.md) — intentional trade-offs and why they exist.
- [Known gaps](docs/architecture/known-gaps.md) — limitations, deferred work, and non-goals.

The active implementation lives under `src/`. The website and MCP server both consume `docs/**/*.md`.

Historical and experimental trees such as `Legacy/`, `FailedAttempt/`, and `new/` are not the authoritative implementation.
