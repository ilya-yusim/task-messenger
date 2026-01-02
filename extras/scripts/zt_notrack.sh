#!/usr/bin/env bash
# Minimal actionable recipe to bypass conntrack (NOTRACK) for ZeroTier UDP port 9993.
# Prefers nftables; falls back to iptables raw table if nft is unavailable.
#
# Usage:
#   sudo ./scripts/zt_notrack.sh apply    # Add NOTRACK rules
#   sudo ./scripts/zt_notrack.sh remove   # Remove NOTRACK rules
#   sudo ./scripts/zt_notrack.sh status   # Show current relevant rules
#
# Idempotent: applying twice won't duplicate rules; remove cleans only what we added.
# Safe scope: only affects UDP traffic with dport/sport 9993.
#
# Rationale: Eliminates nf_conntrack_in cost visible in perf flamegraphs for intensive
# ZeroTier send paths. This keeps other traffic fully tracked.

set -euo pipefail

ZT_PORT=9993
NFT_TABLE=zt_raw
NFT_CHAIN_PRE=preraw
NFT_CHAIN_OUT=outraw

bold() { printf '\e[1m%s\e[0m\n' "$*"; }
note() { printf '[INFO] %s\n' "$*"; }
warn() { printf '\e[33m[WARN]\e[0m %s\n' "$*"; }
err()  { printf '\e[31m[ERR ]\e[0m %s\n' "$*" >&2; }

need_root() {
  if [[ $(id -u) -ne 0 ]]; then
    err "Must run as root (use sudo)."
    exit 1
  fi
}

have_cmd() { command -v "$1" >/dev/null 2>&1; }

nft_apply() {
  note "Applying nftables NOTRACK rules for UDP port ${ZT_PORT}"
  # Create table if missing
  if ! nft list table inet ${NFT_TABLE} >/dev/null 2>&1; then
    nft add table inet ${NFT_TABLE}
  fi
  # Create chains if missing
  if ! nft list chain inet ${NFT_TABLE} ${NFT_CHAIN_PRE} >/dev/null 2>&1; then
    nft add chain inet ${NFT_TABLE} ${NFT_CHAIN_PRE} '{ type filter hook prerouting priority raw; policy accept; }'
  fi
  if ! nft list chain inet ${NFT_TABLE} ${NFT_CHAIN_OUT} >/dev/null 2>&1; then
    nft add chain inet ${NFT_TABLE} ${NFT_CHAIN_OUT} '{ type filter hook output priority raw; policy accept; }'
  fi
  # Add rules if absent
  if ! nft list chain inet ${NFT_TABLE} ${NFT_CHAIN_PRE} | grep -q "udp dport ${ZT_PORT} notrack"; then
    nft add rule inet ${NFT_TABLE} ${NFT_CHAIN_PRE} udp dport ${ZT_PORT} notrack
  fi
  if ! nft list chain inet ${NFT_TABLE} ${NFT_CHAIN_OUT} | grep -q "udp sport ${ZT_PORT} notrack"; then
    nft add rule inet ${NFT_TABLE} ${NFT_CHAIN_OUT} udp sport ${ZT_PORT} notrack
  fi
  note "nftables NOTRACK rules applied."
}

nft_remove() {
  if ! nft list table inet ${NFT_TABLE} >/dev/null 2>&1; then
    warn "Table ${NFT_TABLE} not present; nothing to remove."
    return 0
  fi
  # Delete rules if present
  for CH in ${NFT_CHAIN_PRE} ${NFT_CHAIN_OUT}; do
    if nft list chain inet ${NFT_TABLE} ${CH} >/dev/null 2>&1; then
      # Extract handles for our rules
      mapfile -t HANDLES < <(nft -a list chain inet ${NFT_TABLE} ${CH} | awk "/udp (d|s)port ${ZT_PORT} notrack/ {for(i=1;i<=NF;i++){if(\$i==\"handle\"){print \$(i+1)}}}")
      for H in "${HANDLES[@]:-}"; do
        nft delete rule inet ${NFT_TABLE} ${CH} handle "$H" || true
      done
    fi
  done
  # Remove chains if empty and belong to our table
  for CH in ${NFT_CHAIN_PRE} ${NFT_CHAIN_OUT}; do
    if nft list chain inet ${NFT_TABLE} ${CH} >/dev/null 2>&1; then
      if ! nft list chain inet ${NFT_TABLE} ${CH} | grep -q 'notrack'; then
        nft delete chain inet ${NFT_TABLE} ${CH} || true
      fi
    fi
  done
  # Drop table if now empty
  if nft list table inet ${NFT_TABLE} | grep -q "chain ${NFT_CHAIN_PRE}" || nft list table inet ${NFT_TABLE} | grep -q "chain ${NFT_CHAIN_OUT}"; then
    : # still used
  else
    nft delete table inet ${NFT_TABLE} || true
  fi
  note "nftables NOTRACK rules removed."
}

nft_status() {
  if nft list table inet ${NFT_TABLE} >/dev/null 2>&1; then
    bold "Current nftables rules in table ${NFT_TABLE}:"
    nft list table inet ${NFT_TABLE}
  else
    warn "nftables table ${NFT_TABLE} not present."
  fi
}

iptables_apply() {
  note "Applying iptables raw NOTRACK rules for UDP port ${ZT_PORT}"
  # Avoid duplicates; use -C to check
  if ! iptables -t raw -C PREROUTING -p udp --dport ${ZT_PORT} -j NOTRACK 2>/dev/null; then
    iptables -t raw -A PREROUTING -p udp --dport ${ZT_PORT} -j NOTRACK
  fi
  if ! iptables -t raw -C OUTPUT -p udp --sport ${ZT_PORT} -j NOTRACK 2>/dev/null; then
    iptables -t raw -A OUTPUT -p udp --sport ${ZT_PORT} -j NOTRACK
  fi
  note "iptables NOTRACK rules applied."
}

iptables_remove() {
  for CH in PREROUTING OUTPUT; do
    # Delete all matching rules (there should usually be at most one each)
    while iptables -t raw -S ${CH} 2>/dev/null | grep -q -- "-p udp .* --${CH==PREROUTING?"d":"s"}port ${ZT_PORT} -j NOTRACK"; do
      # Find rule spec
      # shellcheck disable=SC2046
      iptables -t raw -D ${CH} $(iptables -t raw -S ${CH} | grep "-p udp" | grep "${ZT_PORT}" | grep NOTRACK | head -n1 | sed 's/^-A //' ) || break
    done
  done
  note "iptables NOTRACK rules removed."
}

iptables_status() {
  bold "iptables -t raw -S (filtered for port ${ZT_PORT}):"
  iptables -t raw -S | grep -E "9993|PREROUTING|OUTPUT" || true
}

main() {
  [[ $# -ge 1 ]] || { err "Missing subcommand (apply|remove|status)"; exit 1; }
  local sub=$1
  need_root
  if have_cmd nft; then
    case $sub in
      apply) nft_apply ;;
      remove) nft_remove ;;
      status) nft_status ;;
      *) err "Unknown subcommand: $sub"; exit 1; esac
  elif have_cmd iptables; then
    case $sub in
      apply) iptables_apply ;;
      remove) iptables_remove ;;
      status) iptables_status ;;
      *) err "Unknown subcommand: $sub"; exit 1; esac
  else
    err "Neither nft nor iptables found. Install nftables or iptables."
    exit 1
  fi
}

main "$@"
