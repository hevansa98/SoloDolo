#!/usr/bin/env bash
# Installs everything the Pi needs: dump1090 (ADS-B decoder), Dolo, their systemd services and the
# mDNS advertisement for the Solo app. Safe to re-run; existing config files are never overwritten.
#
#   ./deploy/install.sh              install / upgrade
#   ./deploy/install.sh --uninstall  remove Dolo (keeps dump1090 and /etc/dolo)
#   ./deploy/install.sh --purge      same, and also delete /etc/dolo
#
#   GNSS=max-m8q ./deploy/install.sh   also enable GNSS with that receiver (max-m8q | gt-u7 | gpsd)
#
# With GNSS=max-m8q or gt-u7 the installer also turns on the Pi 5's GPIO UART (see enable_gpio_uart below),
# which needs a reboot. The receiver is written to a NEW /etc/dolo/dolo.yaml only; an existing one is left alone.
#
# Not undone by --uninstall/--purge: packages, dump1090's config, the UART setting in /boot/firmware/config.txt
# (originals are saved next to them as *.dolo.bak).
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

case "${1:-}" in
    "" ) ;;
    --uninstall | --purge)
        step "Removing Dolo"
        sudo systemctl disable --now dolo 2>/dev/null || true
        sudo rm -f /etc/systemd/system/dolo.service /etc/avahi/services/dolo.service /usr/local/bin/dolo
        sudo systemctl daemon-reload
        if [[ "$1" == "--purge" ]]; then
            sudo rm -rf /etc/dolo
            echo "Kept: dump1090-mutability. Removed: /etc/dolo"
        else
            echo "Kept: dump1090-mutability, /etc/dolo/dolo.yaml (use --purge to delete it too)"
        fi
        exit 0
        ;;
    *) echo "usage: $0 [--uninstall | --purge]" >&2; exit 2 ;;
esac

case "${GNSS:-}" in
    "" | max-m8q | gt-u7 | gpsd) ;;
    *) echo "GNSS must be max-m8q, gt-u7 or gpsd (got '$GNSS')" >&2; exit 2 ;;
esac

reboot_needed=0

# Raspberry Pi 5: the GPIO14/15 UART becomes /dev/ttyAMA0 once uart0 is enabled, and the serial console must not
# be using it. Idempotent; originals are saved once as *.dolo.bak. Other Pi models map the UART differently, so
# they are left for you to set up by hand (see dolo/README.md).
enable_gpio_uart() {
    local boot=/boot/firmware
    [[ -d $boot ]] || boot=/boot
    local cfg="$boot/config.txt" cmd="$boot/cmdline.txt"
    if ! tr -d '\0' </proc/device-tree/model 2>/dev/null | grep -qE 'Raspberry Pi (5|Compute Module 5)'; then
        echo "Not a Pi 5: enable the GPIO UART yourself (dolo/README.md) so gnss.device exists."
        return
    fi
    if ! grep -qs '^dtparam=uart0=on' "$cfg"; then
        sudo cp -n "$cfg" "$cfg.dolo.bak"
        printf '\n[all]\ndtparam=uart0=on\n' | sudo tee -a "$cfg" >/dev/null  # [all]: not scoped to a model section
        echo "Enabled the GPIO UART in $cfg"
        reboot_needed=1
    fi
    if grep -qsE 'console=(serial0|ttyAMA[0-9]+),[0-9]+' "$cmd"; then
        sudo cp -n "$cmd" "$cmd.dolo.bak"
        sudo sed -i -E -e 's/ ?console=(serial0|ttyAMA[0-9]+),[0-9]+//g' -e 's/^ +//' "$cmd"
        echo "Removed the serial console from $cmd"
        reboot_needed=1
    fi
    [[ $reboot_needed -eq 1 ]] || echo "GPIO UART already enabled"
}

step "Installing packages"
sudo apt-get update -qq
sudo apt-get install -y cmake g++ protobuf-compiler libprotobuf-dev libyaml-cpp-dev nlohmann-json3-dev libgtest-dev \
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
# Receiver position, reused as Dolo's range/bearing reference whenever there is no GNSS fix.
lat="$(sudo sed -n 's/^LAT="\(.*\)"/\1/p' "$dumpcfg" | head -1)"
lon="$(sudo sed -n 's/^LON="\(.*\)"/\1/p' "$dumpcfg" | head -1)"

step "Building Dolo"
cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" -j"$(nproc)"
if ! "$build/dolo_tests" >"$build/test.log" 2>&1; then
    tail -30 "$build/test.log" >&2
    echo "Dolo's tests failed (full log: $build/test.log); not installing." >&2
    exit 1
fi
echo "tests passed"

step "Installing Dolo"
sudo install -Dm755 "$build/dolo" /usr/local/bin/dolo
if [[ -e /etc/dolo/dolo.yaml ]]; then
    echo "/etc/dolo/dolo.yaml exists; leaving it alone"
else
    sudo install -Dm644 "$src/config/dolo.yaml" /etc/dolo/dolo.yaml
    if [[ -n "$lat" && -n "$lon" ]]; then
        sudo sed -i -e "s|^\(  lat:\).*|\1 $lat|" -e "s|^\(  lon:\).*|\1 $lon|" /etc/dolo/dolo.yaml
    fi
    if [[ -n "${GNSS:-}" ]]; then
        sudo sed -i -e 's|^  enabled: false|  enabled: true|' -e "s|^  receiver: .*|  receiver: $GNSS|" /etc/dolo/dolo.yaml
    fi
fi
if [[ -n "${GNSS:-}" && -e /etc/dolo/dolo.yaml ]] && ! sudo grep -q "^  receiver: $GNSS\b" /etc/dolo/dolo.yaml; then
    echo "Note: /etc/dolo/dolo.yaml already existed, so GNSS=$GNSS was not applied. Edit its gnss: section."
fi
if [[ "${GNSS:-}" == "max-m8q" || "${GNSS:-}" == "gt-u7" ]]; then
    step "Enabling the GPIO UART for the $GNSS"
    enable_gpio_uart
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
Config:  /etc/dolo/dolo.yaml   (then: ./deploy/start.sh restart)
Logs:    journalctl -u dolo -f
Status:  ./deploy/start.sh status
MSG
if [[ $reboot_needed -eq 1 ]]; then
    echo
    echo "REBOOT REQUIRED for the GPIO UART change:  sudo reboot"
    echo "(Dolo is running now, but GNSS stays silent until the Pi restarts.)"
fi
