#!/usr/bin/env bash
# Starts the whole Dolo stack through its systemd services: dump1090-mutability (ADS-B decoder), then dolo.
# Installs everything first (deploy/install.sh) if Dolo is not installed yet.
#
#   ./deploy/start.sh            start (default)
#   ./deploy/start.sh restart    restart both, e.g. after editing /etc/dolo/dolo.yaml
#   ./deploy/start.sh stop       stop dolo, then dump1090-mutability
#
# Run as your normal user; sudo is used where needed.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

step() { printf '\n==> %s\n' "$*"; }

if [[ $EUID -eq 0 ]]; then
    echo "Run as your normal user, not root. sudo is used internally." >&2
    exit 1
fi

listening() { ss -ltn | awk -v p=":$1" '$4 ~ p"$" { found = 1 } END { exit !found }'; }

wait_for_port() {  # wait_for_port PORT NAME
    for _ in $(seq 1 30); do
        listening "$1" && return 0
        sleep 0.5
    done
    echo "$2 is not listening on :$1 after 15 s (see: journalctl -u $3 -n 30)" >&2
    return 1
}

action="${1:-start}"
case "$action" in
    start | restart | stop) ;;
    *) echo "usage: $0 [start|restart|stop]" >&2; exit 2 ;;
esac

if [[ "$action" == "stop" ]]; then
    step "Stopping"
    sudo systemctl stop dolo
    sudo systemctl stop dump1090-mutability
    exit 0
fi

if [[ ! -x /usr/local/bin/dolo ]] || ! systemctl cat dolo >/dev/null 2>&1; then
    step "Dolo is not installed; running install.sh"
    exec "$here/install.sh"  # installs, enables and starts every service
fi

step "Starting dump1090-mutability"
sudo systemctl "$action" dump1090-mutability
wait_for_port 30003 "dump1090 (SBS)" dump1090-mutability

step "Starting dolo"
sudo systemctl "$action" dolo
wait_for_port 5555 "dolo" dolo

step "Status"
for svc in dump1090-mutability dolo avahi-daemon; do
    printf '%-24s %s\n' "$svc" "$(systemctl is-active "$svc" || true)"
done
if command -v nc >/dev/null && listening 5556; then
    gnss="$(timeout 3 nc localhost 5556 2>/dev/null | grep -m1 '"gnss"' || true)"
    printf '%-24s %s\n' "gnss" "${gnss:-no GNSS report yet}"
fi
echo
echo "Solo connects to :5555. Logs: journalctl -u dolo -f"
