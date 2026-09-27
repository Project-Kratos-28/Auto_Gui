# Auto_Gui

Operator station for the Kratos rover. One window gives the operator:

- the live GLIM map in an embedded RViz (2D/3D), with waypoints and the Nav2 path
- one-click waypoint logging at the rover's current pose
- autonomous waypoint missions (start, cancel, AUTO/MANUAL handover)
- a grid of up to 16 rover camera streams
- a joystick monitor

The autonomy stack itself (GLIM, waypoint_manager, pcd2pgm, Nav2) lives in
[`auto_fastlio2`](https://github.com/Project-Kratos-28/auto_fastlio2). This repo is the GUI
plus the rover's camera streaming node.

## Repo layout

| Folder | Runs on | What it is |
|---|---|---|
| `kratos_gui/` | Operator laptop | Qt5 + ROS 2 GUI. Details: [`kratos_gui/README.md`](kratos_gui/README.md) |
| `kratos_cameras/` | Rover (Jetson Orin) | Finds cameras, streams them to the laptop with GStreamer (H.265 over UDP or AV1 over TCP) |
| `kratos_msgs/` | Both | Camera stream messages and start/stop services |

## How it connects to auto_fastlio2

| GUI feature | Uses | From |
|---|---|---|
| Log / list / inspect / save waypoints | `/add_waypoint`, `/list_waypoints`, `/get_waypoint`, `/save_waypoints` | GLIM `waypoint_manager` |
| Start / cancel mission | runs `waypoint_mission.py` locally | `kratos_nav` |
| AUTO / MANUAL | `/rover_bridge/set_auto` | `kratos_nav` `rover_bridge.py` |
| Map view | `/glim_ros/*`, `/map`, `/plan`, TF | GLIM, pcd2pgm, Nav2 |

`auto_fastlio2` must be built and sourced on the laptop, both to build the GUI
(`waypoint_interfaces`) and to run missions (`kratos_nav`).

## Requirements

- Ubuntu 22.04, ROS 2 Humble (`ros-humble-desktop`, includes RViz2 and Qt5)
- [`auto_fastlio2`](https://github.com/Project-Kratos-28/auto_fastlio2) cloned at `~/auto_fastlio2`
- This repo cloned at `~/Auto_Gui`

## Getting started (laptop)

Steps 1 to 3 are one-time setup. Step 4 is repeated after any code change or
`git pull`. Step 5 is how you launch it every time. Do them in order: step 4
fails if step 2 was skipped.

> **Don't run `colcon build` inside `~/Auto_Gui`.** It fails (auto_fastlio2's
> packages aren't in that workspace), it also tries to build `kratos_cameras`,
> which only runs on the rover, and it leaves `build/`, `install/` and `log/`
> inside the repo where git picks them up. Build in `~/kratos_gui_ws` (steps 3
> and 4). If you already built in the repo, clean up first:
>
> ```bash
> cd ~/Auto_Gui
> rm -rf build install log
> ```

### Step 1: Install the GStreamer packages

```bash
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-libav gstreamer1.0-plugins-bad
```

`libav` decodes the H.265 camera streams, and `plugins-bad` has the AV1
decoders used for the ZED and gimbal cameras.

### Step 2: Build the auto_fastlio2 packages the GUI needs

```bash
cd ~/auto_fastlio2
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select waypoint_interfaces kratos_nav
```

Expected: `Summary: 2 packages finished`.

`waypoint_interfaces` is needed to build the GUI; `kratos_nav` provides the
`waypoint_mission.py` script the GUI runs when you start a mission.

### Step 3: Create the GUI workspace

```bash
mkdir -p ~/kratos_gui_ws/src
ln -s ~/Auto_Gui/kratos_msgs ~/kratos_gui_ws/src/kratos_msgs
ln -s ~/Auto_Gui/kratos_gui  ~/kratos_gui_ws/src/kratos_gui
```

The workspace sits outside this repo so `build/`, `install/` and `log/` stay
out of git. The symlinks build whichever branch of `~/Auto_Gui` is checked out.

### Step 4: Build the GUI

```bash
cd ~/kratos_gui_ws
source /opt/ros/humble/setup.bash
source ~/auto_fastlio2/install/setup.bash
colcon build --symlink-install --cmake-args -Ddraco_DIR=/usr/lib/x86_64-linux-gnu/cmake/draco
```

Expected: `Summary: 2 packages finished` (about a minute).

The `-Ddraco_DIR` flag points CMake at the system draco library. Without it,
a user-local copy in `~/.local/share/cmake/draco` (if one exists) gets picked
up instead and linking fails with `cannot find -ldraco_shared`. It's harmless
on machines without that copy.

### Step 5: Launch

In a fresh terminal:

```bash
source /opt/ros/humble/setup.bash
source ~/auto_fastlio2/install/setup.bash
source ~/kratos_gui_ws/install/setup.bash
ros2 run kratos_gui kratos_gui
```

To skip the three `source` lines each time, add them to the end of `~/.bashrc`.

### What you should see

The **Kratos Command Center** window opens empty, with an **ADD +** button.
Pick what to open from it (or `Ctrl+T`): Cam View, Joynode, Settings, TUI or
Map View (`M`). The **MAP** tab shows:

- **Left:** RViz with its Displays list. The view stays empty and warns
  "No tf data" until GLIM is running.
- **Right:** the **WAYPOINTS** panel shows `GLIM: OFFLINE` with LOG WAYPOINT
  greyed out, and the **MISSION** panel is below it.
- **Tabs:** the `+` tab (or `Ctrl+T`) adds more tabs, or reopens the MAP
  tab if you closed it (`Ctrl+T` then `M`). Closing MAP only
  hides it: RViz and any running mission keep going. Press `/`
  to see every shortcut.

The map, waypoints and mission come alive once the autonomy stack is running
(see [Running a mission](#running-a-mission)): the panel switches to
`GLIM: ONLINE` and LOG WAYPOINT turns on. Camera streams need the rover side
running (below).

### Troubleshooting

| Problem | Fix |
|---|---|
| `Package 'kratos_gui' not found` | A `source` line from step 5 was skipped |
| `cannot find -ldraco_shared` while building | The `--cmake-args -Ddraco_DIR=...` part of step 4 was dropped |
| `Could not find a package configuration file provided by "waypoint_interfaces"` | Step 2 wasn't run, or `~/auto_fastlio2/install/setup.bash` wasn't sourced before step 4. Also happens when building inside `~/Auto_Gui` instead of `~/kratos_gui_ws` |
| colcon tries to build `kratos_cameras` on the laptop | You're building in `~/Auto_Gui`. Clean up (see the note above) and build in `~/kratos_gui_ws`, which only links `kratos_msgs` and `kratos_gui` |
| `ln: failed to create symbolic link ... File exists` in step 3 | The links already exist from an earlier run. Nothing to do |
| `GLIM: OFFLINE` although GLIM is running | Different `ROS_DOMAIN_ID` from GLIM's terminal, or GLIM started without sourcing auto_fastlio2's install (so no waypoint_manager) |
| Start Mission fails with `kratos_nav not found` | `~/auto_fastlio2/install/setup.bash` wasn't sourced before launching the GUI |
| Window fails with "Unable to create the rendering window" | No usable display. RViz needs a real desktop session (not SSH without X forwarding, not `QT_QPA_PLATFORM=offscreen`) |

## Rover side (cameras)

On the Jetson, with this repo's `kratos_msgs` and `kratos_cameras` built in its
workspace:

```bash
ros2 launch kratos_cameras kratos_cameras.launch.py
```

- The laptop and rover must use the same `ROS_DOMAIN_ID`.
- After changing anything in `kratos_msgs`, rebuild it on **both** machines.
  Mismatched message definitions silently don't connect.

## Running a mission

1. Bring up the autonomy stack as in `auto_fastlio2/docs/LIVE_MISSION_TEST.md`.
   The MAP tab should show `GLIM: ONLINE`.
2. Drive the rover and press **LOG WAYPOINT** (`Ctrl+L`) at each point.
   Rows marked `· pending` aren't bound to a GLIM submap yet. Keep driving
   until they bind.
3. Close the loop so GLIM corrects the map.
4. Tick the waypoints to visit and drag them into order.
5. Press **AUTO** so rover_bridge follows Nav2.
6. Press **START MISSION** and confirm. The mission log appears in the panel.
7. **CANCEL** or `Esc` (from any tab) stops the mission and cancels the Nav2 goal.

Moving a joystick stick drops rover_bridge back to MANUAL. The GUI can't see
that, so the AUTO label only shows the last mode set from the GUI.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| `Ctrl+L` | Log waypoint |
| `Ctrl+D` | Map view 2D / 3D |
| `Esc` | Cancel mission |
| `Ctrl+T` / `Ctrl+W` | Add / close tab (`Ctrl+T`, `M` reopens the MAP tab) |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab |
| `Ctrl+1..9`, `Ctrl+A`, `Ctrl+R` | Camera tab: toggle camera, toggle all, layout |
| `/` | Show all shortcuts |

## Configuration

| Setting | Where | Default |
|---|---|---|
| Rover address (AV1 camera streams) | env `KRATOS_ROVER_HOST` | `192.168.1.10` |
| RViz layout | env `KRATOS_RVIZ_CONFIG` | `kratos_gui/config/kratos.rviz` |
| Laptop address the rover streams to | `kratos_cameras/config/camera_profiles.yaml` (`stream.destination_host`) | `192.168.1.69` |
| Per-camera resolution / bitrate / encoder | same file, `profiles.*` | normal: H.265, ZED and gimbal: AV1 |

## More documentation

- [`kratos_gui/README.md`](kratos_gui/README.md): GUI developer guide (architecture, threading, video pipeline, map/waypoint/mission internals)
- [`kratos_gui/kratos_gui_architecture.md`](kratos_gui/kratos_gui_architecture.md): walkthrough of the Qt/ROS design for new developers
- [`kratos_gui/camerawidget_documentation.md`](kratos_gui/camerawidget_documentation.md): camera tab and streaming internals

## Status

- Tested against simulated waypoint and rover_bridge services: waypoint
  logging, 2D/3D view, AUTO, mission start and cancel.
- Not yet tested on the real rover: live GLIM/Nav2 missions, camera video.
