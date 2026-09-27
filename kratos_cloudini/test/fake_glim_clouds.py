#!/usr/bin/env python3
"""Stand-in for GLIM's point clouds, to test the Cloudini link without the rover.

Publishes, like GLIM (reliable, volatile, frame "map"):
  /glim_ros/points  a 20,000-point scan of a 20 x 12 x 3 m room, 10 Hz
  /glim_ros/map     the "map", 10,000 points bigger every 10 s

The points are random, so they compress worse than real LiDAR scans. Use it to
check that the link works, not to judge the compression ratio.

python3 fake_glim_clouds.py
"""
import numpy as np
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import PointCloud2, PointField
from sensor_msgs_py import point_cloud2
from std_msgs.msg import Header

ROOM = (20.0, 12.0, 3.0)  # x, y, z extent (m)
rng = np.random.default_rng(0)


def room_points(n):
    """n points on the four walls and the floor, with 1 cm noise, plus intensity."""
    sx, sy, sz = ROOM
    surface = rng.integers(0, 5, n)  # 0..3 walls, 4 floor
    u, v = rng.random(n), rng.random(n)
    x = np.select([surface == 0, surface == 1, surface == 2, surface == 3],
                  [np.zeros(n), np.full(n, sx), u * sx, u * sx], u * sx)
    y = np.select([surface == 0, surface == 1, surface == 2, surface == 3],
                  [u * sy, u * sy, np.zeros(n), np.full(n, sy)], v * sy)
    z = np.where(surface == 4, 0.0, v * sz)
    xyz = np.stack([x, y, z], axis=1) + rng.normal(0.0, 0.01, (n, 3))
    intensity = rng.random(n) * 100.0
    return np.column_stack([xyz, intensity]).astype(np.float32)


class FakeGlimClouds(Node):
    def __init__(self):
        super().__init__('fake_glim_clouds')
        self.points_pub = self.create_publisher(PointCloud2, '/glim_ros/points', 10)
        self.map_pub = self.create_publisher(PointCloud2, '/glim_ros/map', 10)
        self.fields = [PointField(name=name, offset=4 * i, datatype=PointField.FLOAT32, count=1)
                       for i, name in enumerate(('x', 'y', 'z', 'intensity'))]
        self.map = room_points(50000)
        self.create_timer(0.1, self.publish_scan)
        self.create_timer(10.0, self.grow_map)
        self.get_logger().info('publishing /glim_ros/points (10 Hz) and /glim_ros/map (every 10 s)')

    def cloud(self, points):
        header = Header(frame_id='map', stamp=self.get_clock().now().to_msg())
        return point_cloud2.create_cloud(header, self.fields, points.tolist())

    def publish_scan(self):
        self.points_pub.publish(self.cloud(room_points(20000)))

    def grow_map(self):
        self.map = np.vstack([self.map, room_points(10000)])
        self.map_pub.publish(self.cloud(self.map))


def main():
    rclpy.init()
    rclpy.spin(FakeGlimClouds())


if __name__ == '__main__':
    main()
