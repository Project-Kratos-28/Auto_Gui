# kratos_cloudini: compressed point clouds over the radio

This package is the **laptop half** of the Cloudini link. The Orin compresses
GLIM's point clouds before they cross the radio, and this package decompresses
them on the laptop so the GUI's RViz can show them.

**Contents**

1. [The idea](#1-the-idea)
2. [How it's split across machines and repos](#2-how-its-split-across-machines-and-repos)
3. [Workflow](#3-workflow)
4. [What's done and what's left](#4-whats-done-and-whats-left)
5. [Requirements](#5-requirements)
6. [Laptop setup, step by step](#6-laptop-setup-step-by-step)
7. [Running it](#7-running-it)
8. [Testing on the laptop without the rover](#8-testing-on-the-laptop-without-the-rover)
9. [The contract with the Orin encoder](#9-the-contract-with-the-orin-encoder)
10. [The Orin side (encoder)](#10-the-orin-side-encoder)
11. [Measuring on the real link](#11-measuring-on-the-real-link)
12. [Design decisions](#12-design-decisions)
13. [Reference: nodes, launch files, parameters](#13-reference-nodes-launch-files-parameters)
14. [Troubleshooting](#14-troubleshooting)
15. [Files](#15-files)

---

## 1. The idea

**The problem.** The operator laptop talks to the rover over a Ubiquiti radio
link. The GUI's RViz shows GLIM's two point clouds:

| Topic | What | How often | Size |
|---|---|---|---|
| `/glim_ros/points` | the live LiDAR scan | ~10 Hz | tens of thousands of points per scan |
| `/glim_ros/map` | the whole map built so far | each time GLIM adds a submap | grows all session |

These are the heaviest things the GUI pulls over the radio, and they compete
with camera video, joystick commands and mission control. The team's own docs
(`auto_fastlio2`, `docs/CONFIGURATION.md`) say point clouds are too heavy to
view over the link.

**What Cloudini is.** [Cloudini](https://github.com/facontidavide/cloudini) is
a compression library made for point clouds: a "zip" for LiDAR data, fast
enough to run live. It rounds coordinates to a precision you choose (1 mm by
default, which you can't see in RViz) and then compresses them. It's lossy in
a controlled way: the error is at most half the chosen precision.

**The plan.** Compress the clouds on the Orin, send the small version over the
radio, and decompress it on the laptop right before RViz:

```
                ORIN (rover)                                   LAPTOP (operator)
GLIM ──raw──► encoder ──compressed──►  radio  ──► decoder ──raw──► GUI RViz
  │
  └──raw──► pcd2pgm, Nav2   (untouched: always full clouds, never cross the radio)
```

Only the copy for the laptop is compressed. Nothing the rover uses to map or
navigate changes.

**When it helps and when it doesn't.**

- It helps when the clouds cross the radio and RViz lags, or camera video
  breaks up while clouds are shown.
- It does nothing when the GUI and GLIM run on the same machine (nothing
  crosses a network).
- It costs a little CPU on the Orin (encoding) and the laptop (decoding).

---

## 2. How it's split across machines and repos

| Half | Runs on | Repo / branch | Status |
|---|---|---|---|
| **Decoder** (this package) + GUI launch file + RViz config | Laptop, ROS 2 Humble | `Auto_Gui`, branch `cloudini-implementation` | done, tested locally |
| **Encoder** (two stock Cloudini converter nodes) | Orin, ROS 2 Jazzy, inside the `kratos_glim` Docker container | `auto_fastlio2`, a branch off `jazzy-nvblox` | not written yet |
| **Switch** (TUI tab "Cloudini" checkbox, `start_tui.sh` row 8) | Laptop GUI → Orin `start.sh` | `Auto_Gui` (`tuipanel.cpp`, `orin_system_control`) and `auto_fastlio2` (`start_tui.sh`) | exists: passes `cloudini:=true` to the Orin launch, which currently ignores it |

The two halves are tied together only by the
[contract](#9-the-contract-with-the-orin-encoder): topic names, message type,
QoS and Cloudini version. Either half can be built and tested on its own as
long as it follows the contract.

---

## 3. Workflow

1. **Laptop half first** (done): write the decoder, test it on one laptop
   with a stand-in encoder ([section 8](#8-testing-on-the-laptop-without-the-rover)).
2. **Agree on the contract** with whoever owns `jazzy-nvblox`
   ([section 9](#9-the-contract-with-the-orin-encoder)).
3. **Encoder**, on its own branch off `jazzy-nvblox` (never directly on
   `jazzy-nvblox`), following [section 10](#10-the-orin-side-encoder). Open it
   as a **draft** pull request until it has run on the Orin.
4. **Quick manual test on the Orin** before rebuilding anything: install
   Cloudini in the running container by hand, start the converters by hand,
   and check the laptop decodes. This settles the biggest unknown,
   Humble↔Jazzy, in about 30 minutes.
5. **Real test**: rebuild the image, `./start.sh cloudini:=true`, GUI with
   `cloudini:=true`.
6. **Measure** ([section 11](#11-measuring-on-the-real-link)) with Cloudini
   on and off. If it doesn't clearly help, don't merge.
7. **Merge** both halves: the encoder PR into `jazzy-nvblox`, and
   `cloudini-implementation` into `Auto_Gui` `main`.

Git: fixes to the GUI in general go to `Auto_Gui` `main` first, then get merged
into `cloudini-implementation`, so the Cloudini branch stays current. The
Cloudini work lives in its own files (`kratos_cloudini/`,
`kratos_gui/launch/`, `kratos_cloudini.rviz`), so those merges don't conflict.

---

## 4. What's done and what's left

**Done (laptop half, commit `aa9b42f` on `cloudini-implementation`)**

- `cloudini_decoder` node with explicit QoS per topic ([why](#12-design-decisions)).
- `decoder.launch.py`: one decoder for the scan, one for the map.
- `kratos_gui/launch/kratos_gui.launch.py`: `cloudini:=true` starts the
  decoders and the GUI together; closing the GUI stops them.
- `kratos_gui/config/kratos_cloudini.rviz`: shows the decoded clouds; raw
  displays included but switched off.
- `test/fake_glim_clouds.py`: stand-in for GLIM's clouds.
- Tested on one laptop (details in [section 8](#8-testing-on-the-laptop-without-the-rover)).

**Left**

- [ ] Encoder on the Orin ([section 10](#10-the-orin-side-encoder)).
- [ ] Cloudini in the Orin's Docker image.
- [ ] Humble (laptop) ↔ Jazzy (Orin) check for the compressed message type.
      ROS doesn't officially support mixing distros. The team's docs say
      standard messages work over Fast DDS; this one needs testing.
- [ ] Real measurements with real LiDAR data. The local test used random
      points, which compress badly (ratio 0.54). Real scans should do much
      better.

**Open issues (not Cloudini's, found along the way)**

- `jazzy-nvblox` removed `rover_bridge.py`, so the GUI's AUTO/MANUAL buttons
  (`/rover_bridge/set_auto`) have nothing to talk to on the Orin.
- With the stock converter as the encoder, a GUI started *after* GLIM's last
  map update waits for the next submap before the map appears. See
  [section 12](#12-design-decisions) for the possible fix.

---

## 5. Requirements

### Laptop (operator)

| What | Version / value | Why |
|---|---|---|
| Ubuntu | 22.04 | ROS 2 Humble target |
| ROS 2 | Humble (`ros-humble-desktop`) | the GUI, RViz |
| `ros-humble-cloudini-ros` | **1.1.0** (apt) | provides `cloudini_lib`, which the decoder links against |
| `ros-humble-point-cloud-interfaces` | apt | the `CompressedPointCloud2` message type, needed at runtime |
| Everything the GUI needs | see the [main README](../README.md#requirements) | GStreamer, `auto_fastlio2` built, etc. |
| `RMW_IMPLEMENTATION` | `rmw_fastrtps_cpp` (Humble's default) | must match the Orin |
| `ROS_DOMAIN_ID` | same as the Orin (`0` by default) | or the laptop can't see the rover |
| `ROS_LOCALHOST_ONLY` | unset | or the laptop can't see the rover |

### Orin (rover)

| What | Version / value | Why |
|---|---|---|
| `ros-jazzy-cloudini-ros` | **1.1.0** (apt), installed **in the Docker image** (`docker/Dockerfile.glim`) | the encoder |
| `ros-jazzy-point-cloud-interfaces` | apt, in the image | the message type |
| `ros_network.env` | `RMW_IMPLEMENTATION=rmw_fastrtps_cpp`, `ROS_DOMAIN_ID` = laptop's, `ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET` | already set this way |

### Cloudini version rule

Both machines must use the **apt** Cloudini (1.1.0 on both Humble and Jazzy,
compressed format **version 4**). Newer Cloudini built from source (1.3.x)
writes format **version 5**, which 1.1.0 refuses to read. If one side is ever
upgraded, upgrade both.

---

## 6. Laptop setup, step by step

This assumes the GUI already builds and runs per the
[main README](../README.md#getting-started-laptop) (workspace
`~/kratos_gui_ws`, `auto_fastlio2` built).

**Step 1: Switch the repo to the Cloudini branch**

```bash
cd ~/Auto_Gui
git fetch
git switch cloudini-implementation
```

The workspace symlinks build whichever branch is checked out.

**Step 2: Install Cloudini and its message type** (needs your password)

```bash
sudo apt install ros-humble-cloudini-ros ros-humble-point-cloud-interfaces
```

Check the version:

```bash
apt-cache policy ros-humble-cloudini-ros | head -3     # Installed: 1.1.0-...
```

**Step 3: Link the package into the workspace** (once)

```bash
ln -s ~/Auto_Gui/kratos_cloudini ~/kratos_gui_ws/src/kratos_cloudini
```

`File exists` means it's already linked, which is fine.

**Step 4: Build**

```bash
cd ~/kratos_gui_ws
source /opt/ros/humble/setup.bash
source ~/auto_fastlio2/install/setup.bash
colcon build --symlink-install --cmake-args -Ddraco_DIR=/usr/lib/x86_64-linux-gnu/cmake/draco
```

Expected: `Summary: 3 packages finished`. A `Manually-specified variables were
not used by the project: draco_DIR` warning for `kratos_cloudini` is harmless.

**Step 5: Check the install**

```bash
source ~/kratos_gui_ws/install/setup.bash
ros2 pkg executables kratos_cloudini          # kratos_cloudini cloudini_decoder
ros2 interface show point_cloud_interfaces/msg/CompressedPointCloud2 | head -3
ros2 launch kratos_gui kratos_gui.launch.py --show-args   # lists 'cloudini'
```

---

## 7. Running it

Every time, in a fresh terminal:

```bash
source /opt/ros/humble/setup.bash
source ~/auto_fastlio2/install/setup.bash
source ~/kratos_gui_ws/install/setup.bash
```

**With the rover:**

1. Launch the GUI with the decoders:

   ```bash
   ros2 launch kratos_gui kratos_gui.launch.py cloudini:=true
   ```

2. In the GUI's **TUI** tab, tick **Cloudini**, then press **START**. This
   starts the Orin stack with `cloudini:=true`. The Orin side has to exist
   for this to do anything ([section 10](#10-the-orin-side-encoder)).

**What you should see:**

- In the terminal, once at start:
  ```
  [cloudini_decoder_points]: /glim_ros/points/compressed (best_effort) -> /kratos_gui/glim_points (volatile)
  [cloudini_decoder_map]: /glim_ros/map/compressed (reliable) -> /kratos_gui/glim_map (latched)
  ```
- Every 10 s, while clouds arrive:
  ```
  [cloudini_decoder_points]: 98 clouds in 10 s, 4.00 MB received -> 7.36 MB decoded (ratio 0.54)
  ```
  "ratio" is compressed ÷ original: lower is better.
- In RViz (MAP tab), the Displays list has **GLIM scan (Cloudini)** and
  **GLIM map (Cloudini)** switched on, and **GLIM scan/map (raw, not
  compressed)** switched off. The config path in the MAP toolbar ends in
  `kratos_cloudini.rviz`.

**Without Cloudini** (Orin not compressing): launch the GUI normally
(`ros2 launch kratos_gui kratos_gui.launch.py`, or `ros2 run kratos_gui
kratos_gui`). If you launched with `cloudini:=true` anyway, turn on the "raw,
not compressed" displays in RViz. They pull the full clouds over the radio.

**Stopping:** close the GUI. The launch file then stops both decoders.

---

## 8. Testing on the laptop without the rover

This runs the whole chain on one machine: a fake GLIM, Cloudini's stock
converter as a stand-in for the Orin encoder, and the real decoders + GUI.
Use your own `ROS_DOMAIN_ID` so nothing leaks onto the network.

Open four terminals. In **each** one:

```bash
source /opt/ros/humble/setup.bash
source ~/auto_fastlio2/install/setup.bash
source ~/kratos_gui_ws/install/setup.bash
export ROS_DOMAIN_ID=78
```

**Terminal 1: GUI + decoders** (started first on purpose: start order must not matter)

```bash
ros2 launch kratos_gui kratos_gui.launch.py cloudini:=true
```

**Terminals 2 and 3: stand-in encoders** (what the Orin will run)

```bash
ros2 run cloudini_ros cloudini_topic_converter --ros-args -r __node:=enc_points \
  -p compressing:=true -p topic_input:=/glim_ros/points \
  -p topic_output:=/glim_ros/points/compressed -p resolution:=0.001
```

```bash
ros2 run cloudini_ros cloudini_topic_converter --ros-args -r __node:=enc_map \
  -p compressing:=true -p topic_input:=/glim_ros/map \
  -p topic_output:=/glim_ros/map/compressed -p resolution:=0.001
```

**Terminal 4: fake GLIM** (a 20 × 12 m room: 10 Hz scan, map grows every 10 s)

```bash
python3 ~/Auto_Gui/kratos_cloudini/test/fake_glim_clouds.py
```

**Expect:**

- Within a few seconds, RViz shows a box-shaped room (the scan), and after
  ~10 s the map.
- Terminal 1 logs "clouds in 10 s ... ratio ~0.54". The fake points are random,
  so they compress worse than real scans.
- Extra checks in a fifth terminal (same setup lines):
  ```bash
  ros2 topic bw /glim_ros/points              # raw
  ros2 topic bw /glim_ros/points/compressed   # compressed: should be smaller
  ros2 topic echo --once --field width /kratos_gui/glim_map   # a late subscriber still gets the map
  ```

**What this test showed** (run on 2026-09-27):

| Check | Result |
|---|---|
| Decoders started before the encoders and before GLIM | worked: start order doesn't matter |
| Scans received and matched to originals by timestamp | 60 / 60, 20,000 points each |
| Largest position error at 1 mm resolution | 0.5 mm (expected: half the step) |
| Intensity, `frame_id` | intact |
| Late subscriber to the decoded map | got it in 0.03 s (latched) |
| GUI with `cloudini:=true` | drew the decoded clouds; closing it stopped both decoders |
| Compression on random test points | 0.54 (3.3 MB/s → 1.8 MB/s) |

---

## 9. The contract with the Orin encoder

Both halves must follow this exactly.

| | Scan | Map |
|---|---|---|
| Encoder input (from GLIM) | `/glim_ros/points` | `/glim_ros/map` |
| **Encoder output** | **`/glim_ros/points/compressed`** | **`/glim_ros/map/compressed`** |
| Output type | `point_cloud_interfaces/msg/CompressedPointCloud2` | same |
| Output reliability | reliable or best effort (the decoder takes either) | **reliable** (the decoder subscribes reliable) |
| Output durability | volatile | volatile |
| Laptop output (decoded) | `/kratos_gui/glim_points`, reliable, volatile | `/kratos_gui/glim_map`, reliable, transient local |
| Cloudini | apt 1.1.0 (format v4) on both sides | same |
| Resolution | encoder's choice, default 0.001 m | same |

- The `/compressed` suffix is what Cloudini's stock converter uses by default,
  so the Orin side can be the stock converter.
- The decoder doesn't need to know the resolution: it's stored in each
  compressed message.
- If a name changes, change it on both sides. On the laptop, the names are
  launch arguments of `decoder.launch.py` (`points_in`, `map_in`).

---

## 10. The Orin side (encoder)

Not written yet. This is the complete recipe, for whoever writes it (see
[workflow](#3-workflow)). All paths are in the `auto_fastlio2` repo, branch
off `jazzy-nvblox`. On the Orin, the repo is `~/kratos_glim`.

**Facts it depends on** (checked in the source):

- GLIM (v1.2.2, `rviz_viewer.cpp`) publishes `/glim_ros/points` as reliable,
  volatile, depth 10, and `/glim_ros/map` as reliable, **transient local**,
  depth 1.
- `kratos_bringup/launch/kratos.launch.py` starts GLIM inside
  `TimerAction(period=3.0)`, i.e. 3 s after everything else.
- `start.sh` passes its arguments straight to
  `ros2 launch kratos_bringup kratos.launch.py`. `ros2 launch` silently
  ignores undeclared arguments, which is why `cloudini:=true` does nothing today.
- The container is started with `--rm`: anything installed by hand inside it
  is gone after `./stop.sh`.
- The workspace is built with `--symlink-install`, so launch-file edits take
  effect without rebuilding.

**Change 1: `docker/Dockerfile.glim`.** Add to the existing big
`apt-get install` list (no new `RUN` layer):

```
ros-${ROS_DISTRO}-cloudini-ros ros-${ROS_DISTRO}-point-cloud-interfaces
```

Then rebuild the image: `./stop.sh && ./docker/build_image.sh`.

**Change 2: `src/kratos_bringup/package.xml`**

```xml
<exec_depend>cloudini_ros</exec_depend>
```

**Change 3: `src/kratos_bringup/launch/kratos.launch.py`.** A sketch of what
to add (adapt it to the file's style):

```python
from launch.conditions import IfCondition
from launch_ros.parameter_descriptions import ParameterValue

# in generate_launch_description():
def cloudini_encoder(name):
    return Node(
        package='cloudini_ros', executable='cloudini_topic_converter',
        name=f'cloudini_{name}', output='screen',
        parameters=[{
            'compressing': True,
            'topic_input': f'/glim_ros/{name}',
            'topic_output': f'/glim_ros/{name}/compressed',
            'resolution': ParameterValue(arg('cloudini_resolution'), value_type=float),
        }],
        condition=IfCondition(arg('cloudini')))

# Started at launch, BEFORE GLIM (which waits 3 s): the converter picks its QoS
# once from the publishers it sees at startup. Seeing none, it uses reliable +
# volatile, which is what the laptop decoder expects. Don't move these into
# GLIM's TimerAction.
encoders = [cloudini_encoder('points'), cloudini_encoder('map')]

# in the LaunchDescription list:
DeclareLaunchArgument('cloudini', default_value='false',
                      description='true: also publish Cloudini-compressed GLIM clouds for the laptop'),
DeclareLaunchArgument('cloudini_resolution', default_value='0.001',
                      description='metres, for the compressed clouds'),
*encoders,
```

**Rules for the encoder:**

1. **Start the converters at launch, not in GLIM's timer.** That order gives
   them reliable + volatile QoS, matching the decoder.
2. **Pass `resolution` as a float** (`ParameterValue(..., value_type=float)`).
   Launch arguments are strings; `1` would arrive as an integer and the node
   would refuse it.
3. **Set `topic_output` explicitly** to the contract names.
4. **Don't touch anything else:** GLIM, pcd2pgm, Nav2 and perception keep the
   raw topics. No remapping.
5. **`cloudini` defaults to `false`**, so a normal `./start.sh` is unchanged.
6. The converter skips compressing when nobody subscribes to its output, so
   with no laptop listening it costs almost nothing.

**Testing it:**

1. *Quick, before touching the image* (settles Humble↔Jazzy):
   ```bash
   ./start.sh
   docker exec -u root kratos_glim bash -c \
     "apt-get update && apt-get install -y ros-jazzy-cloudini-ros ros-jazzy-point-cloud-interfaces"
   ./docker/run_container.sh      # shell in the container, then:
   ros2 run cloudini_ros cloudini_topic_converter --ros-args -r __node:=cloudini_points \
     -p compressing:=true -p topic_input:=/glim_ros/points \
     -p topic_output:=/glim_ros/points/compressed -p resolution:=0.001
   # (and the same for map, in a second container shell)
   ```
   Then run the GUI on the laptop with `cloudini:=true` ([section 7](#7-running-it)).
2. *Real*, after the three changes: `./stop.sh`, `./docker/build_image.sh`,
   `./start.sh cloudini:=true` (or tick Cloudini in the GUI's TUI tab and
   press START).
3. *Checks inside the container:*
   ```bash
   ros2 topic list | grep compressed                     # both topics
   ros2 topic info -v /glim_ros/map/compressed           # Reliability: RELIABLE
   ```
   `./logs.sh cloudini` shows "Converted N messages, average compression
   ratio ..." once the laptop is listening.

**Later improvement (optional): the map for a late GUI.** GLIM's map is
latched, but the stock converter started before GLIM republishes it as
volatile. A GUI started after the last map update then waits for the next
submap. The fix is to keep it latched across the link. That needs **both** an
encoder that publishes the map as transient local (the stock converter can't
guarantee this, so it means a small custom encoder like this decoder) **and**
the decoder's map input switched to transient local. Change both together: a
transient-local subscriber never matches a volatile publisher, so changing
only one side breaks the map entirely.

---

## 11. Measuring on the real link

Whether Cloudini stays depends on these numbers. Take them with Cloudini on
and off, with the cameras streaming as they would in the field.

| What | Where | How |
|---|---|---|
| Raw vs compressed bandwidth | **on the Orin** (measuring raw from the laptop would pull it over the radio) | `ros2 topic bw /glim_ros/points` vs `/glim_ros/points/compressed`, same for the map |
| Real compression ratio | laptop | the decoder's 10 s log line |
| Encoder CPU | Orin | `top` in the container: the `cloudini_topic_converter` processes (GLIM is already CPU-heavy) |
| Decoder CPU | laptop | `top`: `cloudini_decoder` |
| Does RViz keep up? | laptop | watch the scan in RViz; compare with the raw displays |
| Does camera video suffer? | laptop | the CAMS tab while clouds are shown, on vs off |

---

## 12. Design decisions

**Why a custom decoder instead of Cloudini's `cloudini_topic_converter`.**
The stock converter picks its QoS **once, at startup**, from whoever is
already publishing. On the laptop the GUI usually starts before the Orin's
encoder, so the converter would guess. A wrong guess, such as a reliable
subscription against a best-effort publisher, means the laptop silently never
receives anything. `cloudini_decoder` sets its QoS explicitly per topic, so
start order doesn't matter. Like the stock converter, it works on raw
serialized messages: the compressed message is never unpacked into a C++
struct, and the decoded cloud is published straight from the decoder's own
buffer instead of being copied into a new message first.

**Why the scan decoder subscribes best effort.** A best-effort subscription
matches both reliable and best-effort publishers. And on a lossy radio, a lost
packet drops one frame instead of stalling the stream behind retransmissions.
A newer scan arrives 0.1 s later anyway.

**Why the map decoder subscribes reliable and republishes latched.** The map
is big and only sent when GLIM adds a submap, so losing one hurts: reliable
makes DDS resend lost pieces. Republishing it as transient local ("latched")
means RViz gets the last map as soon as it subscribes (display re-enabled, RViz
config reloaded) instead of waiting for the next submap.

**Why the stock converter is fine on the Orin.** There, its startup behaviour
is predictable: it always starts before GLIM (which waits 3 s), sees no
publisher, and uses reliable + volatile. That receives both of GLIM's topics
and matches the decoder.

**Why two RViz configs instead of switching topics in code.** The GUI already
supports `KRATOS_RVIZ_CONFIG`. `kratos_gui.launch.py cloudini:=true` points it
at `kratos_cloudini.rviz`, so the GUI's C++ code didn't change at all.

**Why the raw displays are in the Cloudini config but off.** For when the
Orin isn't compressing. Turning one on in RViz subscribes to the full cloud
over the radio, which is the thing Cloudini exists to avoid, so they start
off.

---

## 13. Reference: nodes, launch files, parameters

### `cloudini_decoder` (node)

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `input_topic` | string | (required) | compressed topic to subscribe to |
| `output_topic` | string | (required) | where to publish the decoded `PointCloud2` |
| `reliable` | bool | `false` | input QoS: `false` = best effort (matches any publisher), `true` = reliable (retransmits, only matches reliable publishers) |
| `latch_output` | bool | `false` | output QoS: `true` = transient local (keeps the last cloud for late subscribers, always decodes), `false` = volatile (skips decoding when nobody listens) |
| `stats_period` | double (s) | `10.0` | how often to log counts and ratio; `0` = never |

Input queue depth is 1 (only the newest cloud matters). Output is always
reliable, depth 1.

### `decoder.launch.py`

```bash
ros2 launch kratos_cloudini decoder.launch.py [points_in:=...] [points_out:=...] [map_in:=...] [map_out:=...]
```

| Argument | Default |
|---|---|
| `points_in` | `/glim_ros/points/compressed` |
| `points_out` | `/kratos_gui/glim_points` |
| `map_in` | `/glim_ros/map/compressed` |
| `map_out` | `/kratos_gui/glim_map` |

Starts `cloudini_decoder_points` (`reliable:=false`, `latch_output:=false`)
and `cloudini_decoder_map` (`reliable:=true`, `latch_output:=true`).

### `kratos_gui.launch.py` (in `kratos_gui`)

```bash
ros2 launch kratos_gui kratos_gui.launch.py [cloudini:=true|false]
```

- `cloudini:=false` (default): only the GUI, same as `ros2 run kratos_gui kratos_gui`.
- `cloudini:=true`: includes `decoder.launch.py` and starts the GUI with
  `KRATOS_RVIZ_CONFIG` = `kratos_gui/config/kratos_cloudini.rviz`.
- Either way, when the GUI exits, the whole launch shuts down.

---

## 14. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Could not find a package configuration file provided by "cloudini_lib"` when building | Cloudini not installed | [Step 2](#6-laptop-setup-step-by-step) |
| Decoder dies at start: `failed to load type support` / `point_cloud_interfaces` | message type not installed | `sudo apt install ros-humble-point-cloud-interfaces` |
| Decoder dies at start: `input_topic and output_topic must both be set` | node run by hand without parameters | use `decoder.launch.py`, or pass both `-p` parameters |
| Cloudini displays empty, and `ros2 topic list` on the laptop has no `/compressed` topics | the Orin isn't compressing: Cloudini not ticked before START, encoder not implemented yet, or it failed on the Orin | tick Cloudini in the TUI tab and restart; check `./logs.sh cloudini` on the Orin |
| No `/glim_ros/...` topics on the laptop at all | network: different `ROS_DOMAIN_ID`, `ROS_LOCALHOST_ONLY` set, or the radio drops DDS multicast discovery | match the domain; unset `ROS_LOCALHOST_ONLY`; on the Orin, set `ROS_STATIC_PEERS=<laptop IP>` in `ros_network.env` and restart |
| `/compressed` topics listed, but the decoder log never shows "clouds in 10 s" | QoS mismatch (e.g. the map encoder publishes best effort), or Humble↔Jazzy doesn't work for this type | `ros2 topic info -v /glim_ros/map/compressed` on the laptop: the publisher must be RELIABLE. If QoS is right, it's the distro mix: report it |
| Decoder logs `cannot decompress: Unsupported encoding version` | the two sides run different Cloudini versions | use apt 1.1.0 on both ([version rule](#cloudini-version-rule)) |
| The map appears only after a while | GUI started after GLIM's last map update; the next submap brings it | expected with the stock encoder; see "Later improvement" in [section 10](#10-the-orin-side-encoder) |
| RViz is still slow with Cloudini | a raw display is on, or the camera streams are the real load | check that both "raw, not compressed" displays are unchecked; compare with the CAMS tab closed |
| Ratio in the log is poor (e.g. > 0.5) on real data | resolution very fine, or very noisy clouds | try `cloudini_resolution:=0.005` on the Orin (5 mm, still fine for viewing) |

---

## 15. Files

| Path | What |
|---|---|
| `kratos_cloudini/src/cloudini_decoder.cpp` | the decoder node |
| `kratos_cloudini/launch/decoder.launch.py` | two decoders (scan, map) |
| `kratos_cloudini/test/fake_glim_clouds.py` | stand-in for GLIM's clouds, for [section 8](#8-testing-on-the-laptop-without-the-rover) |
| `kratos_cloudini/CMakeLists.txt`, `package.xml` | build; depends on `rclcpp`, `cloudini_lib`, and at runtime `point_cloud_interfaces` |
| `kratos_gui/launch/kratos_gui.launch.py` | GUI launch with the `cloudini` switch |
| `kratos_gui/config/kratos_cloudini.rviz` | RViz config on the decoded topics |
