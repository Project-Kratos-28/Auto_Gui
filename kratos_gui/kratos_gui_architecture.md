# Kratos GUI Architecture Walkthrough

Welcome! This document provides a highly detailed walkthrough of the Kratos Rover GUI codebase. It's written as a comprehensive guide for a new developer tasked with understanding the layout, design patterns (like signals/slots and multithreading), the video streaming pipeline, and ROS 2 integration.

---

## 1. High-Level Concept: Why a ROS Node?

Starting with your question: **Why is this package a ROS Node instead of just a basic C++ application?**

A robot running ROS 2 (like the Kratos Rover) communicates using a specialized middleware (DDS). To talk to the rover—whether requesting to start a camera or reading joystick inputs—we must speak the language of ROS. If this were a plain C++ program, you wouldn’t be able to subscribe to topics or invoke services. 

By making the GUI a ROS 2 Node (`rclcpp::Node`), the application natively participates in the rover's data network. It seamlessly sends service calls (`kratos_msgs::srv::StartCameraStream`) to the rover back-end and subscribes to data feeds (`/joy`) just like any other component in your robotics stack.

---

## 2. Bootstrapping the Application (`main.cpp`)

The `main.cpp` file blends the ROS framework with the Qt GUI framework:

```cpp
int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);   // 1. Initialize ROS 2 networking
  QApplication a(argc, argv); // 2. Initialize the Qt GUI environment
  MainWindow w;               
  w.show();                   // 3. Show the UI
  int result = a.exec();      // 4. Start the blocking Qt Event Loop
  rclcpp::shutdown();         // 5. Cleanly destruct ROS on UI close
  return result;
}
```

The heart of every UI application is the **Event Loop** (`a.exec()`). It is an infinite loop that constantly blocks and waits for mouse clicks, keyboard presses, or system events, acting upon them instantly. If the main thread stops to perform a long task (like decoding video), the event loop stops processing clicks, causing a "Frozen UI."

---

## 3. Qt Fundamentals: Signals and Slots

Throughout the codebase, you will see `emit`, `signals:`, and `slots:`. This is Qt's observer pattern for communication.

- **Signals**: Just function signatures. You use `emit mySignal(data)` to broadcast that an event happened.
- **Slots**: The receiver functions that actually execute the logic.
- **Connect**: You bind them together using `connect(sender, &Sender::signal, receiver, &Receiver::slot)`.

**Why is it built this way?**
Decoupling. The `VideoWorker` processes video frames but has no idea *what* the GUI looks like. It just yells `emit newFrame(image)`. The UI (which knows how to render) listens for that signal and updates the screen. Furthermore, **Signals and Slots are thread-safe**. When you connect a signal from a background thread to the main GUI thread, Qt automatically packages the call into an event and ships it across thread boundaries safely (known as a `QueuedConnection`).

---

## 4. The ROS Integration (`RosWorker`)

File: `ros_worker.h` / `ros_worker.cpp`

The `RosWorker` class handles all communication with the rover. Instead of locking up the UI thread listening for ROS messages, it uses a very clever mechanism:

**The Spin Timer Mechanism**
When `init()` is called, we set up a `QTimer` (`spinTimer_`) firing every 10 milliseconds:

```cpp
spinTimer_ = new QTimer(this);
connect(spinTimer_, &QTimer::timeout, this, [this]() {
  if (rclcpp::ok()) {
    rclcpp::spin_some(node_); // Check ROS for 10ms
  }
});
spinTimer_->start(10);
```

Instead of spinning ROS in a dedicated C++ thread (which sometimes requires complicated mutex locks to share data with the GUI), `RosWorker` tells ROS to rapidly check for inbound messages (`spin_some`) on the main UI thread, but without blocking. 

**What are the methods doing?**
- `onCameraListReceived()`: When the rover publishes available cameras, this ROS callback fires. We read the C++ `std::vector`, repackage it into a clean Qt type (`CameraStatusList`), and `emit camerasUpdated(...)`. 
- `callStartStream(QString)`: This takes a UI-driven string, converts it to an RPC request, and asynchronously sends it to the rover to turn a camera on.

---

## 5. Multithreading & The Video Player (`VideoPlayerWidget.cpp`)

To render video streams smoothly without stuttering the GUI, we *must* use Multithreading. 

In `VideoPlayerWidget::VideoPlayerWidget(...)`, we construct a background thread:
```cpp
videoWorker_ = new VideoWorker();
videoWorker_->moveToThread(&videoThread_);
videoThread_.start();
```
`moveToThread` is a Qt powerhouse. It takes the entire `VideoWorker` object and teleports its event loop processing into a newly spawned `QThread`.

From this point on, if we want to talk to `VideoWorker`, we cannot just call `videoWorker_->startPipeline()`. That would execute the function in the *current* thread!
Instead, we do this:
```cpp
QMetaObject::invokeMethod(videoWorker_, "startPipeline", Qt::QueuedConnection, ...);
```
This packages the `startPipeline` command and safely beams it into the background thread's execution queue.

---

## 6. The Heart of the Video Logic (`video_worker.cpp`)

This file is where the heavy lifting happens. We receive video via **GStreamer**, a massively powerful open-source pipeline architecture. 

### The Pipeline Structure
```cpp
"udpsrc port=%1 ! application/x-rtp,media=video,encoding-name=H265,payload=96 ! rtph265depay ! h265parse ! avdec_h265 ! videoconvert ! videoscale ! video/x-raw,width=1280,height=720,format=RGB ! appsink name=sink emit-signals=true sync=false"
```

Logically, this reads linearly from left to right:
1. **`udpsrc`**: Listen on a UDP port for network packets.
2. **`rtph265depay` & `h265parse`**: Strip the networking (RTP) headers off to reveal the raw H.265 encoded byte stream.
3. **`avdec_h265`**: Run CPU-based H.265 decoding to convert the compressed stream into individual raw frames.
4. **`videoconvert`**: Transform whatever obscure color profile came out (likely YUV) into a standard RGB profile format.
5. **`appsink`**: Provide an access point for our customized C++ code to reach in and grab the finished frame.

### The Memory Flow Strategy (CRITICAL)
When GStreamer yields a frame, it calls `onNewSample`. Memory management here is paramount. GStreamer manages its own internal, highly optimized memory pools (`GstBuffer`). If we mess up here, the entire program segfaults.

Here is the exact memory transition happening inside `onNewSample`:

1. **Mapping:** `gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)` 
   This safely locks GStreamer's memory and gives a C-pointer (`mapInfo.data`) to the raw pixel bytes.
2. **Shallow Wrapping:** `QImage frame(mapInfo.data, ...)` 
   We wrap a Qt Image around that pointer. *Warning: Qt does NOT own this memory! If we passed this image to the UI, by the time the UI drew it, GStreamer would have deleted the memory, causing an instant crash.*
3. **Deep Copy:** `QImage copied = frame.copy();` 
   We explicitly command Qt to allocate brand new RAM on the heap and duplicate all the pixels. `copied` is now perfectly safe and owned by Qt.
4. **Unmapping:** `gst_buffer_unmap()` releases GStreamer's memory lock.
5. **Passing Across Threads:** `emit self->newFrame(copied)` 
   This ships the `copied` object across the thread boundaries over to the `VideoPlayerWidget` in the main GUI thread, where the `videoLabel_->setPixmap(...)` actually draws the pixels to the screen.

---

## 7. The User Interface Controls (`CameraWidget.cpp`)

The `CameraWidget` acts as the master orchestrator.

**State Tracking:** It uses `CameraStatusList knownCameras_;` to track what cameras actually exist on the robot and whether they are active.

**Dynamic Rebuilding:** 
When the user selects "2x2" from the dropdown, `rebuildGrid(2, 2)` fires. This dynamically loops, deleting old Qt UI elements (destroying layouts) and allocating brand new `GridCell` combinations (a new Layout, a Dropdown ComboBox, and a nested `VideoPlayerWidget`). 

**User Interactions:** 
If a user clicks a button to turn a camera on (`onSidebarCameraClicked`), the UI simply leverages the `RosWorker` to send a service call to the rover. At this moment, the UI assumes nothing. It waits.

When the rover successfully turns the camera on, the rover publishes a new message to `/kratos/available_cameras`. The `RosWorker` handles the message, `emit`s the UI change, and `CameraWidget::onCamerasUpdated` redraws the UI styles. This creates a purely **Event-Driven Architecture**—the UI state is guaranteed to reflect the robot's actual state.
