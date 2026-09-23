# Kratos GUI Developer Guide

Welcome to the `kratos_gui` development guide. This document explains the architecture, features, how to interact with the GUI, and how to build the package.

## 1. Architecture Overview

The `kratos_gui` is a Qt6-based monitoring dashboard allowing operators to view multiple camera streams, manage layouts, and monitor joystick node outputs on the Kratos Rover.

The core architecture consists of four main components:
- `MainWindow`: The outer shell and tab manager. It controls the main window state, the top-level application hotkeys, and the instantiation of new monitoring tabs (`CameraWidget`, `JoyNodeWidget`).
- `CameraWidget`: A dedicated tab for viewing camera streams. It implements a fully dynamic `QGridLayout` allowing users to view up to 16 cameras simultaneously. It handles layout selection, sizing, and stream assignments.
- `JoyNodeWidget`: A dedicated tab for viewing raw or processed joystick data.
- `RosWorker`: The bridge between the Qt GUI thread and the ROS2 environment. It runs on a dedicated `QThread` and uses signals (`camerasUpdated`) and slots (`callStartStream`, `callStopStream`) to communicate states back and forth robustly without blocking the UI.

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
| `Ctrl + T` | Open Add Tab Dialog (use arrow keys or 'C'/'J' hotkeys + Enter) |
| `Ctrl + W` | Close the currently focused tab |
| `Ctrl + Shift + Q` | Immediatly terminate the GUI application |
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
- ROS 2 (Humble or later)
- Qt 6 (Core, Gui, Widgets)
- A configured `kratos-rover` workspace

### Building
Navigate to the root of your workspace (e.g., `~/ros2_ws`) and run:
```bash
colcon build --packages-select kratos_gui
```

### Sourcing and Running
Source the workspace and run the node:
```bash
source install/setup.bash
ros2 run kratos_gui kratos_gui_exec
```

*Tip: For debugging Qt layouts visually, consider using the `QT_DEBUG_PLUGINS=1` environment variable.*
