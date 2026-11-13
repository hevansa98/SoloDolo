#!/usr/bin/env bash
# Installs everything the Pi needs: dump1090 (ADS-B decoder), Dolo, their systemd services and the
# mDNS advertisement for the Solo app. Safe to re-run; existing config files are never overwritten.
#
#   ./deploy/install.sh              install / upgrade
#   ./deploy/install.sh --uninstall  remove Dolo (keeps dump1090 and /etc/dolo)
#
# Run as your normal user; sudo is used where needed.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
src="$(dirname "$here")"
build="$src/build"

step() { printf '\n==> %s\n' "$*"; }

if [[ $EUID -eq 0 ]]; then
    echo "Run as your normal user, not root (the build should not be root-owned). sudo is used internally." >&2
    exit 1
fi

if [[ "${1:-}" == "--uninstall" ]]; then
    step "Removing Dolo"
    sudo systemctl disable --now dolo 2>/dev/null || true
    sudo rm -f /etc/systemd/system/dolo.service /etc/avahi/services/dolo.service /usr/local/bin/dolo
    sudo systemctl daemon-reload
    echo "Kept: dump1090-mutability, /etc/dolo/dolo.yaml"
    exit 0
fi

step "Installing packages"
sudo apt-get update -qq
sudo apt-get install -y cmake g++ protobuf-compiler libprotobuf-dev libyaml-cpp-dev nlohmann-json3-dev \
    dump1090-mutability rtl-sdr avahi-daemon

step "Preparing the RTL-SDR"
# Keep the kernel's DVB-T driver from claiming the dongle.
sudo tee /etc/modprobe.d/blacklist-rtlsdr.conf >/dev/null <<'CONF'
blacklist dvb_usb_rtl28xxu
blacklist rtl2832
blacklist rtl2830
CONF
# USB access for non-root users (the package normally ships this; add it if missing).
if ! grep -qs 'idProduct}=="2838"' /etc/udev/rules.d/*.rules /lib/udev/rules.d/*.rules 2>/dev/null; then
    echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="0bda", ATTRS{idProduct}=="2838", GROUP="plugdev", MODE="0666"' |
        sudo tee /etc/udev/rules.d/20-rtlsdr.rules >/dev/null
fi
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=usb || true

step "Configuring dump1090-mutability"
dumpcfg=/etc/default/dump1090-mutability
set_cfg() {  # set_cfg KEY VALUE: replace an existing KEY="..." line, or append one
    if sudo grep -q "^$1=" "$dumpcfg"; then
        sudo sed -i "s|^$1=.*|$1=\"$2\"|" "$dumpcfg"
    else
        echo "$1=\"$2\"" | sudo tee -a "$dumpcfg" >/dev/null
    fi
}
set_cfg START_DUMP1090 yes
set_cfg SBS_OUTPUT_PORT 30003
set_cfg NET_BIND_ADDRESS 127.0.0.1   # SBS is only for Dolo on this Pi
set_cfg FIX_CRC yes
set_cfg GAIN max
if [[ -n "${LAT:-}" && -n "${LON:-}" ]]; then
    set_cfg LAT "$LAT"; set_cfg LON "$LON"
fi
# Receiver position, reused as Dolo's range/bearing reference until a GNSS HAT is fitted.
lat="$(sudo sed -n 's/^LAT="\(.*\)"/\1/p' "$dumpcfg" | head -1)"
lon="$(sudo sed -n 's/^LON="\(.*\)"/\1/p' "$dumpcfg" | head -1)"

step "Building Dolo"
cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" -j"$(nproc)"
"$build/dolo_tests" >/dev/null && echo "tests passed"

step "Installing Dolo"
sudo install -Dm755 "$build/dolo" /usr/local/bin/dolo
if [[ -e /etc/dolo/dolo.yaml ]]; then
    echo "/etc/dolo/dolo.yaml exists; leaving it alone"
else
    sudo install -Dm644 "$src/config/dolo.yaml" /etc/dolo/dolo.yaml
    if [[ -n "$lat" && -n "$lon" ]]; then
        sudo sed -i -e "s|^\(  lat:\).*|\1 $lat|" -e "s|^\(  lon:\).*|\1 $lon|" /etc/dolo/dolo.yaml
    fi
fi
sudo install -m644 "$here/dolo.service" /etc/systemd/system/dolo.service
sudo install -Dm644 "$here/dolo.avahi.service" /etc/avahi/services/dolo.service

step "Starting services"
sudo systemctl daemon-reload
sudo systemctl enable --now avahi-daemon
sudo systemctl enable dump1090-mutability
sudo systemctl restart dump1090-mutability
sudo systemctl enable dolo
sudo systemctl restart dolo
sleep 2

step "Status"
for svc in dump1090-mutability dolo avahi-daemon; do
    printf '%-24s %s\n' "$svc" "$(systemctl is-active "$svc" || true)"
done
cat <<MSG

Dolo is listening on :5555 (Solo) and :5556 (JSON debug: nc localhost 5556).
Config:  /etc/dolo/dolo.yaml   (then: sudo systemctl restart dolo)
Logs:    journalctl -u dolo -f
MSG
