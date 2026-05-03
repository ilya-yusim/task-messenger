# Rendezvous service

`tm-rendezvous` is the network-side half of Task Messenger. A
rendezvous server runs on a public host (typically a small cloud VM)
and provides:

- A **discovery broker** so dispatchers can publish their ZeroTier
  identity and workers can find them.
- A **network-wide dashboard** for end users — the same browser
  dashboard a dispatcher operator sees locally, but rendered from
  snapshots relayed by every dispatcher registered with this
  rendezvous.
- HTTP endpoints (`/api/monitor`, `/healthz`, static dashboard
  assets) served by an in-process HTTP server.

## Components

| File | Scope |
| --- | --- |
| [main.cpp](main.cpp) | Entry point; parses configuration and starts the server. |
| [RendezvousServer.{hpp,cpp}](RendezvousServer.hpp) | Lifecycle and HTTP routing. |
| [RendezvousHttpHandler.cpp](RendezvousHttpHandler.cpp) | Static-asset mounting and JSON snapshot response. |
| [SnapshotListener.{hpp,cpp}](SnapshotListener.hpp) | Receives monitoring snapshots from registered dispatchers. |

The browser assets served by the rendezvous are the shared bundle in
[dashboard/](../../dashboard/README.md). At runtime the server resolves
that directory via the same dev/installed walk-up the dispatcher uses,
so a single source of truth ships to both.

## Operating model

A rendezvous instance is associated with a single ZeroTier identity
that is shared with the dispatchers it brokers; this identity lives in
the rendezvous deployment and is referenced by dispatcher configs as
the rendezvous endpoint.

Today the rendezvous dashboard renders one dispatcher at a time.
Aggregating multiple dispatchers into a single network-wide view is on
the roadmap.

## Configuration and deployment

Configuration is read from `config/config-rendezvous.json`; see
[config/README.md](../../config/README.md). End-to-end deployment on a
cloud VM (cloud-init, systemd, reverse proxy, dynamic DNS) is covered
by the operational guide under
[.github/prompts/](../../.github/prompts/) and the helper scripts in
[extras/scripts/](../../extras/).

## Related documentation

- Top-level overview: [README.md](../../README.md).
- Per-dispatcher (local) monitoring: [dispatcher/README.md](../../dispatcher/README.md).
- Dashboard browser assets and runtime contract: [dashboard/README.md](../../dashboard/README.md).
