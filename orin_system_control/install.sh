#!/usr/bin/env bash
# Installs system_control.py as a systemd user service on the Orin, so the GUI's TUI tab
# works right after boot with nobody at the Orin's terminal. --remove uninstalls.
set -euo pipefail
DIR=$(cd "$(dirname "$0")" && pwd)
UNIT=~/.config/systemd/user/kratos-system-control.service

if [ "${1:-}" = --remove ]; then
    systemctl --user disable --now kratos-system-control 2>/dev/null || true
    rm -f "$UNIT"; systemctl --user daemon-reload; echo removed; exit 0
fi

mkdir -p "$(dirname "$UNIT")"
cat > "$UNIT" <<EOF
[Unit]
Description=Kratos system_control (GUI TUI tab services)
After=network-online.target docker.service

[Service]
# Same ROS_DOMAIN_ID / discovery / peers as the kratos_glim container
EnvironmentFile=%h/kratos_glim/ros_network.env
ExecStart=/bin/bash -c 'source /opt/ros/jazzy/setup.bash && exec python3 $DIR/system_control.py'
Restart=always
RestartSec=3
# stop.sh / start.sh children must outlive a restart of this node
KillMode=process

[Install]
WantedBy=default.target
EOF
systemctl --user daemon-reload
systemctl --user enable --now kratos-system-control
loginctl show-user "$USER" -p Linger | grep -q yes ||
    echo "NOTE: run 'sudo loginctl enable-linger $USER' once, or it only starts after a login"
systemctl --user --no-pager status kratos-system-control | head -5
