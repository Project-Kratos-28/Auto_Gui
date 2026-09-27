# orin_system_control

The Orin side of the GUI's MAIN CONTROL tab. `system_control.py` serves `/kratos_system/*`
(`std_srvs/Trigger`) and runs the matching part of `~/kratos_glim` (`start.sh`, `stop.sh`,
the `start_tui.sh` checks), so nobody needs a terminal on the Orin.

Install on the Orin (host, not the container; needs ROS 2 Jazzy in `/opt/ros/jazzy`):

    mkdir -p ~/kratos_system_control && cp system_control.py install.sh ~/kratos_system_control/
    bash ~/kratos_system_control/install.sh
    sudo loginctl enable-linger $USER    # once: start at boot without a login

Uninstall: `bash ~/kratos_system_control/install.sh --remove`.
Logs: `journalctl --user -u kratos-system-control`.

The GUI and the Orin need the same `ROS_DOMAIN_ID` (`~/kratos_glim/ros_network.env`, 0) and a
shared IPv4 network. The Humble (GUI) ↔ Jazzy (Orin) link works for services; the
"sequence size exceeds remaining buffer" warnings are Humble failing to parse Jazzy's
discovery extras and are harmless, but `ros2 node list` / `ros2 param` from Humble can't see
the node (call the services directly instead).
