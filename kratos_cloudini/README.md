# kratos_cloudini

Laptop side of the Cloudini link. The Orin compresses GLIM's point clouds with
[Cloudini](https://github.com/facontidavide/cloudini) before they cross the radio;
this package decompresses them on the laptop for the GUI's RViz.

```
Orin    GLIM ─► encoder ─► /glim_ros/{points,map}/compressed ──radio──►
Laptop  cloudini_decoder ─► /kratos_gui/glim_{points,map} ─► GUI RViz (kratos_cloudini.rviz)
```

GLIM, pcd2pgm and Nav2 on the Orin keep using the full, uncompressed clouds. Only
the copy for the laptop is compressed.

## Contract with the Orin encoder

Both halves must agree on this. The encoder lives in `auto_fastlio2`, branch
`jazzy-nvblox`, behind the `cloudini:=true` launch argument that `start_tui.sh`
and the GUI's TUI tab already pass.

| | Scan | Map |
|---|---|---|
| Orin publishes (compressed) | `/glim_ros/points/compressed` | `/glim_ros/map/compressed` |
| Type | `point_cloud_interfaces/msg/CompressedPointCloud2` | same |
| Encoder QoS | reliable or best effort | **reliable** (the map decoder subscribes reliable) |
| Laptop republishes | `/kratos_gui/glim_points` (volatile) | `/kratos_gui/glim_map` (transient local) |

- **Cloudini version:** use the apt packages on both machines
  (`ros-humble-cloudini-ros` on the laptop, `ros-jazzy-cloudini-ros` on the Orin, both
  1.1.0, encoding format v4). A newer Cloudini built from source on one side only
  (1.3.x writes format v5) can't be read by 1.1.0, and the decoder logs
  `cannot decompress: Unsupported encoding version`.
- **Resolution** is the encoder's choice (`resolution`, in metres; 0.001 = 1 mm is
  visually lossless). The decoder doesn't need to know it.
- The `/compressed` names are what Cloudini's stock `cloudini_topic_converter` uses by
  default, so the Orin side can be just two converter nodes:

  ```bash
  ros2 run cloudini_ros cloudini_topic_converter --ros-args -r __node:=cloudini_points \
    -p compressing:=true -p topic_input:=/glim_ros/points -p resolution:=0.001
  ros2 run cloudini_ros cloudini_topic_converter --ros-args -r __node:=cloudini_map \
    -p compressing:=true -p topic_input:=/glim_ros/map -p resolution:=0.001
  ```

## Why a custom decoder instead of cloudini_topic_converter

The stock converter chooses its QoS once, at startup, by looking at who is already
publishing. The GUI normally starts before the Orin's encoder, so the converter would
guess. A wrong guess (for example a reliable subscription against a best-effort
publisher) means the laptop silently never receives anything. `cloudini_decoder` sets
the QoS explicitly per topic, so start order doesn't matter:

- **Scan:** subscribes best effort, which matches any publisher. A lost radio packet
  drops one frame instead of stalling the stream behind retransmissions.
- **Map:** subscribes reliable, because it's large and only sent when GLIM adds a
  submap. It republishes transient local, so RViz gets the last map as soon as it
  subscribes instead of waiting for the next submap.
- Volatile outputs with no subscriber aren't decompressed at all.
- Every 10 s it logs clouds, MB received and MB decoded, so you can see the real
  compression ratio on the link.

## Use

Normally through the GUI launch file:

```bash
ros2 launch kratos_gui kratos_gui.launch.py cloudini:=true
```

This starts both decoders and the GUI with `config/kratos_cloudini.rviz`. Closing the
GUI stops the decoders. On its own:

```bash
ros2 launch kratos_cloudini decoder.launch.py
ros2 launch kratos_cloudini decoder.launch.py points_in:=/other/compressed/topic
```

Launch arguments: `points_in`, `points_out`, `map_in`, `map_out`.

Node parameters (`cloudini_decoder`): `input_topic`, `output_topic`, `reliable`
(default false), `latch_output` (default false), `stats_period` (s, default 10, 0 = off).

## Tested

On one laptop (ROS_DOMAIN_ID isolated), with a fake GLIM publishing a 20,000-point scan
at 10 Hz and a growing map, and the stock converter as the encoder:

- Decoders started before the encoder and before GLIM: all 60 scans in the test window
  arrived and matched their originals by timestamp.
- Maximum position error 0.5 mm at 1 mm resolution (as expected: half the step),
  intensity and `frame_id` intact.
- A late subscriber got the latched map in 0.03 s.
- The GUI launched with `cloudini:=true` drew the decoded clouds, and closing it
  stopped both decoders.

Not tested yet: the real Orin encoder, the Humble↔Jazzy link, and real LiDAR data.
The test clouds were random noise and compressed to only 0.54. Real scans have much
more structure and should compress far better. Measure with the decoder's log or
`ros2 topic bw`.
