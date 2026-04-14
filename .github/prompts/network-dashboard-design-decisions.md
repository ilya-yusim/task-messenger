# Network Monitoring Dashboard - Design Decisions

## Architecture Decisions (Session Confirmed)

### 1. Dashboard Server Location
- **Prototype (v1, 7 days):** Co-located in dispatcher process for speed
- **Future (v2+):** Separated as part of a Router service that manages worker/generator registration
- **Rationale:** Router will coordinate multiple generators and workers; dashboard will be part of that service. Starting in-process avoids refactor now if we use socket-based interface from day one

### 2. ZeroTier Socket Communication Impact (CRITICAL)
- Dashboard server MUST use ZeroTier sockets to communicate with generators/workers
- This applies whether dashboard is in-process or out-of-process
- **Design decision:** Create monitoring interface that is socket-based (over ZeroTier) FROM THE START
  - Even though v1 prototype may access it in-process via function calls, the interface must be designed as if it's networked
  - This allows v2 separation with zero architectural refactor

## Monitoring Interface Strategy
- **Interface design:** Well-defined snapshot/stream protocol that can run over ZeroTier TCP sockets
  - Enables future router/dashboard to connect over ZeroTier without rewrite
  - Initial implementation: function call wrapper that yields same data
  - Can be swapped to actual socket transport later with client-side changes only

- **Two possible transport layers for initial browsing:**
  - Option A: HTTP server on dispatcher's ZeroTier IP (requires browser on ZeroTier network)
  - Option B: Monitoring protocol over ZeroTier → local HTTP proxy/dashboard service (cleaner separation)
  - Decision: Recommend **Option B** for future-proofing, but Option A faster for learning/demo

## Worker Visibility Model
- Workers connect TO dispatcher (inbound)
- Dispatcher has complete view of connected workers (no worker-to-worker comms needed)
- Dashboard reads dispatcher's session state + transport stats
- When router is added: router will have generator/worker registry; dashboard can query router instead

## Configuration Notes
- Dispatcher binds two ports:
  - Port 8080 (or configured): worker connections
  - Port 8081 (or new config): monitoring interface (ZeroTier-based)
- Config extends existing [config-dispatcher.json](config-dispatcher.json) with monitoring_port option
