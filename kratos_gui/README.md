# Kratos GUI Developer Guide

> Setup, build, run and the operator walkthrough are in the [repo README](../README.md). This file covers how the GUI works inside.

Welcome to the `kratos_gui` development guide. This document explains the architecture, features, how to interact with the GUI, and how to build the package.

## 1. Architecture Overview

The `kratos_gui` is a Qt5-based operator dashboard for the Kratos Rover. It embeds RViz for the live GLIM map, tags GLIM waypoints, runs the autonomous waypoint mission, shows multiple camera streams, and monitors joystick node outputs.

It is Qt5 (not Qt6) because it embeds RViz, and RViz2 on Humble is built against Qt5; the two Qt versions can't share one process.

The core architecture consists of these components:
- `MainWindow`: The outer shell and tab manager. It controls the main window state, the top-level application hotkeys, the MAP tab, and the instantiation of new monitoring tabs (`CameraWidget`, `JoyNodeWidget`).
- `MapWidget` (the MAP tab): created once at startup and never destroyed. Closing the tab only hides it, so RViz, the waypoint list and a running mission survive; "Open Map View" (`Ctrl+T`, `M`) brings the same instance back. It holds a full embedded RViz (`rviz_common::VisualizationFrame`) loaded from `config/kratos.rviz`, a 2D/3D view toggle, and the two panels below.
- `WaypointPanel`: logs a waypoint at the rover's current pose (`/add_waypoint`), lists waypoints with their pending/bound state (`/list_waypoints`, polled every 1 s), shows a waypoint's current pose (`/get_waypoint`) and saves them (`/save_waypoints`). These are served by GLIM's `waypoint_manager` extension in `auto_fastlio2`.
- `MissionPanel`: switches `rover_bridge` between MANUAL and AUTO (`/rover_bridge/set_auto`) and runs `kratos_nav`'s `waypoint_mission.py` on the ticked waypoints.
- `CameraWidget`: A dedicated tab for viewing camera streams. It implements a fully dynamic `QGridLayout` allowing users to view up to 16 cameras simultaneously. It handles layout selection, sizing, and stream assignments.
- `JoyNodeWidget`: A dedicated tab for viewing raw or processed joystick data.
- `RosWorker`: The bridge between the Qt GUI thread and the ROS2 environment. It runs on a dedicated `QThread` and uses signals (`camerasUpdated`, `waypointsListed`, ...) and slots (`callStartStream`, `callAddWaypoint`, `callSetAutoMode`, ...) to communicate states back and forth robustly without blocking the UI. RViz uses its own node (`kratos_gui_rviz`) and executor.

### Map, waypoints and mission

- **2D/3D:** the toolbar button (or `Ctrl + D`) switches RViz between `TopDownOrtho` and `Orbit`, keeping the focal point.
- **Displays:** `config/kratos.rviz` is `auto_fastlio2/glim/glim_ros.rviz` plus the pcd2pgm `/map` (transient local), the waypoint markers on `/glim_ros/waypoints` and the Nav2 plan on `/plan`. Set `KRATOS_RVIZ_CONFIG=/path/to/other.rviz` to load a different one. RViz's own File menu still works for saving a changed layout.
- **Waypoint list = mission plan:** ticked waypoints are driven to top to bottom; drag rows to reorder. A new waypoint is added at the bottom, ticked. Rows marked `· pending` are not bound to a GLIM submap yet: they can't be resolved and the mission waits up to 30 s for them (keep driving so the submap closes).
- **Save:** the YAML path is on the machine running GLIM, not necessarily this one.
- **Start Mission** asks for confirmation, then runs `waypoint_mission.py -p waypoints:=[...]` and streams its log into the panel. The script is run directly with `python3`, not through `ros2 run`, because `ros2 run` does not forward SIGTERM and cancelling would leave the script (and its Nav2 goal) running.
- **Cancel** (`Esc`, from any tab) sends SIGTERM; the script cancels its Nav2 goal before exiting. If it hasn't exited after 10 s it is killed and the log says to check the rover. Closing the GUI with a mission running asks first, then waits for the same cleanup.
- **AUTO/MANUAL:** the wheels only follow Nav2 when `rover_bridge` is in AUTO. Moving a joystick stick drops it back to MANUAL, which the GUI cannot see, so the label only shows the last mode set from the GUI.

## 2. Key Features & Under the Hood Logic

This application uses a sophisticated but robust architecture to handle asynchronous video effectively on Linux, bypassing the heavy Qt event loop where necessary. Here's exactly how everything works and why it was built this way:

### Dynamic Grid Layout Reconstruction
The `CameraWidget` uses a `QGridLayout`. When the user changes layouts (e.g., from 1x1 to 3x3), `rebuildGrid(int rows, int cols)` triggers a complete tear-down of the existing layout constraints. It iterates over the custom `QVector<GridCell> cells_` struct array, stops all active `VideoPlayerWidget`s, destroys `gridLayout_`, and clears all widget stretch factors (`setRowStretch(r, 0)`). 

To uniquely identify which cell dropdown invoked a change without complex subclasses, it ingeniously uses Qt Dynamic Properties:
```cpp
picker->setProperty("cellIndex", i);
connect(picker, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, i](int) { onCellStreamChanged(i); });
```

### ROS 2 Communication & Abstraction (`RosWorker`)
`RosWorker` acts as an isolation layer wrapping an `rclcpp::Node` (`kratos_gui_node`). It subscribes to `/kratos/available_cameras` (Type: `kratos_msgs::msg::CameraStreamList`). 

When it receives new hardware states, it packages this into a `QList<CameraInfo>` (`CameraStatusList`). Because `RosWorker` runs on a dedicated `QThread` and `CameraWidget` runs on the main GUI thread, they use `qRegisterMetaType` to safely ferry this data across the thread boundary via a `Qt::QueuedConnection`. 

Start and stop operations invoke `/kratos/cameras/start_stream` and `stop_stream` asynchronously (`async_send_request`). The callback fires securely over the Qt bus when the Orin acknowledges the command.

### Zero-Latency GStreamer Pipeline (`VideoWorker`)
The most heavily "vibe-coded" magic happens here. Once a viewer routes a stream to a cell, `VideoPlayerWidget::switchStream` drops an execution call to a dedicated `QThread` housing a `VideoWorker` instance.

The pipeline explicitly leverages UDP transport to bypass TCP congestion protocols (which would cause video "lag" if a packet dropped). The pipeline expects `nvv4l2h265enc` (H.265 Jetson Hardware Encoding) on the sender side:
```bash
udpsrc port=%1
! application/x-rtp,media=video,encoding-name=H265,payload=96
! rtph265depay
! h265parse
! avdec_h265           # CPU Decoding payload
! videoconvert
! videoscale
! video/x-raw,width=1280,height=720,format=RGB
! appsink name=sink emit-signals=true sync=false
```
**CRITICAL LOGIC:** The `sync=false` attribute on the appsink is mandatory for achieving zero-latency streaming. It dictates that GStreamer must completely ignore its internal clocking mechanism and shove frames to the application the absolute microsecond they finish decoding.

### Safe Memory Handoff Across Threads
When the `appsink` receives a frame, it fires the static callback `VideoWorker::onNewSample()`.
1. A `GstSample` is pulled natively via `gst_app_sink_pull_sample`. 
2. `gst_buffer_map` grants C-level read-only access to the raw RGB payload.
3. Because unmapping the GStreamer buffer immediately destroys `mapInfo.data`, creating a `QImage` pointer directly to it would crash the app once the image hit the GUI thread. To solve this, `QImage::copy()` is invoked to force a deep heap allocation, securely severing the memory from GStreamer before emitting the `newFrame` signal across threads.

### Persistent Streams
Rather than killing pipelines when tabs are switched (which bottlenecks the hardware), pipelines stay alive persistently in the background. Stopping streams is strictly manual via the sidebar dropdowns ensuring massive optimization to connection speeds and memory fragmentation.

## 3. Keyboard Shortcuts

The GUI is designed to heavily favor keyboard-centric power users. There are global shortcuts (available anywhere) and view-specific shortcuts.

Press `/` anywhere in the application to pop up the Cheat Sheet overlay.

### Global Application Shortcuts (`MainWindow`)
| Shortcut | Action |
|----------|--------|
| `Ctrl + Tab` | Next Tab (cycles forward through the open views) |
| `Ctrl + Shift + Tab` | Previous Tab (cycles backward) |
| `Ctrl + T` | Open Add Tab Dialog (use arrow keys or 'C'/'J'/'S'/'M' hotkeys + Enter; 'M' opens or jumps to the MAP tab) |
| `Ctrl + W` | Close the currently focused tab |
| `Ctrl + Shift + Q` | Quit the GUI (asks first if a mission is running) |
| `Ctrl + L` | Log a waypoint at the rover's current pose (name box, or the next free `wpN`) |
| `Ctrl + D` | Toggle the map view between 2D and 3D |
| `Esc` | Cancel the running mission |
| `/` | Show keyboard shortcut cheat sheet overlay |

### Camera Widget Shortcuts (`CameraWidget`)
*Note: These shortcuts only respond when you are actively inside a Camera Tab.*
| Shortcut | Action |
|----------|--------|
| `Ctrl + 1` through `Ctrl + 9` | Toggles the Nth camera (based on sidebar alphabetical order) explicitly On/Off. |
| `Ctrl + A` | Batch toggles all cameras On. If all are currently On, it toggles them all Off. |
| `Ctrl + R` | Rapid-focus the Layout Dropdown box, allowing arrow-key selection of grid layouts (e.g. 2x2, 4x4). |

## 4. Build & Run Instructions

### Prerequisites
- ROS 2 Humble with RViz2 (`ros-humble-desktop`)
- Qt 5 (comes with ROS Humble)
- GStreamer: `sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-libav gstreamer1.0-plugins-bad` (libav decodes H.265; plugins-bad has the AV1 decoders used for the ZED and gimbal cameras)
- The `auto_fastlio2` workspace built and sourced, for `waypoint_interfaces` (build time) and `kratos_nav` (to start missions)

### Building
Copy or symlink `kratos_msgs` and `kratos_gui` into a workspace, then:
```bash
source /opt/ros/humble/setup.bash
source ~/auto_fastlio2/install/setup.bash     # waypoint_interfaces, kratos_nav
colcon build --packages-select kratos_msgs kratos_gui
```
`kratos_msgs/CameraStream` gained an `encoder` field, so rebuild `kratos_cameras` on the rover too. Mismatched message definitions don't connect.

If linking fails with `cannot find -ldraco_shared`, CMake has picked up a user-local draco (`~/.local/share/cmake/draco`) instead of the system one. Point it back with:
```bash
colcon build --packages-select kratos_gui --cmake-args -Ddraco_DIR=/usr/lib/x86_64-linux-gnu/cmake/draco
```

### Sourcing and Running
```bash
source ~/auto_fastlio2/install/setup.bash
source install/setup.bash
ros2 run kratos_gui kratos_gui
```
- `KRATOS_ROVER_HOST` (default `192.168.1.10`) is the rover address used for AV1 (TCP) camera streams.
- `KRATOS_RVIZ_CONFIG` overrides the RViz config.
- The GUI needs a real display for RViz; it won't run with `QT_QPA_PLATFORM=offscreen`.

*Tip: For debugging Qt layouts visually, consider using the `QT_DEBUG_PLUGINS=1` environment variable.*
