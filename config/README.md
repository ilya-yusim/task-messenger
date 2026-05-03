# Configuration files

Reference configurations for the three Task Messenger services. Each
file is consumed by exactly one component; the schema and option
semantics are documented in that component's README.

| File | Consumed by | Component README |
| --- | --- | --- |
| [config-dispatcher.json](config-dispatcher.json) | `tm-dispatcher` (via a generator binary). | [dispatcher/README.md](../dispatcher/README.md) |
| [config-worker.json](config-worker.json) | `tm-worker`. | [worker/README.md](../worker/README.md) |
| [config-rendezvous.json](config-rendezvous.json) | `tm-rendezvous`. | [services/rendezvous/README.md](../services/rendezvous/README.md) |

## ZeroTier identity

The [vn-rendezvous-identity/](vn-rendezvous-identity/) directory holds
the ZeroTier identity shared between the rendezvous service and the
dispatchers it brokers. Only `identity.public` and `identity.secret`
are version-controlled; runtime state is generated on first launch.

## Installed locations

When Task Messenger is installed via the packaged distributions
(Homebrew, the Windows installer, the Linux installer), each
component's config lives under that component's per-OS configuration
directory. See [docs/INSTALLATION.md](../docs/INSTALLATION.md).
