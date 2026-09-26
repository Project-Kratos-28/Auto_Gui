#!/usr/bin/env python3
"""system_control - the GUI's TUI tab, served as ROS services on the Orin.

Does what each key of ~/kratos_glim/start_tui.sh does, without a terminal. Runs on
the Orin host (not in the kratos_glim container, which start/stop create and remove)
as a systemd user service; see install.sh. All services are std_srvs/Trigger:

  /kratos_system/start      apply the settings, then start.sh --no-follow <launch args>
  /kratos_system/stop       stop.sh (GLIM saves its session to ~/kratos_glim/maps/)
  /kratos_system/diagnose   start_tui.sh's checks: docker, build, LiDAR port, MID-360,
                            ZED, ROS network, lidar_z. Reports; changes nothing.
  /kratos_system/health     one message on each key topic within 15 s? + log errors
  /kratos_system/log        last lines of log/bringup/latest.log (log_filter: regex)
  /kratos_system/status     container / stack up, uptime
  /kratos_system/get_config the current settings as key=value lines
  /kratos_system/rviz       RViz on the Orin's own screen (if someone is logged in)

Settings are ROS parameters (mid360_ip, orin_lidar_ip, laptop_peer, depth, lidar_z,
gui, cloudini). They are written where the stack reads them, exactly like
start_tui.sh does: MID360_config.json, ros_network.env and .home/tui.env. ROS_DOMAIN_ID
is shown but never changed from here: a different domain would cut the GUI off.
"""
import glob
import os
import re
import subprocess
import threading
import time

import rclpy
from rcl_interfaces.msg import ParameterDescriptor, SetParametersResult
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import MultiThreadedExecutor
from rclpy.node import Node
from std_srvs.srv import Trigger

NAME = 'kratos_glim'   # container
IP_RE = re.compile(r'^(\d{1,3}\.){3}\d{1,3}$')
CHOICES = {'depth': ('zed', 'ess', 'none'), 'gui': ('auto', 'true', 'false')}


def sh(cmd, timeout=30):
    """Run a shell command; (exit code, merged output). Never raises."""
    try:
        p = subprocess.run(['bash', '-c', cmd], stdin=subprocess.DEVNULL, capture_output=True,
                           text=True, timeout=timeout)
        return p.returncode, (p.stdout + p.stderr).strip()
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode(errors='replace') if isinstance(e.stdout, bytes) else (e.stdout or '')
        return 124, (out.strip() + f'\n(timed out after {timeout} s)').strip()


def strip_ansi(s):
    return re.sub(r'\x1b\[[0-9;?]*[A-Za-z]', '', s)


class SystemControl(Node):
    def __init__(self):
        super().__init__('kratos_system')
        self.repo = self.declare_parameter('repo', os.path.expanduser('~/kratos_glim')).value
        self.livox = f'{self.repo}/src/livox_ros_driver2/config/MID360_config.json'
        self.netenv = f'{self.repo}/ros_network.env'
        self.tuienv = f'{self.repo}/.home/tui.env'

        # Settings start as whatever the files say, so the GUI shows the real values
        cur = self.read_config()
        any_type = ParameterDescriptor(dynamic_typing=True)
        for k in ('mid360_ip', 'orin_lidar_ip', 'laptop_peer', 'depth', 'lidar_z', 'gui', 'cloudini'):
            self.declare_parameter(k, cur[k], any_type)
        self.declare_parameter('log_filter', '', any_type)
        self.add_on_set_parameters_callback(self.validate)

        self.busy = threading.Lock()   # start / diagnose / health: one at a time
        group = ReentrantCallbackGroup()
        for name in ('start', 'stop', 'diagnose', 'health', 'log', 'status', 'get_config', 'rviz'):
            self.create_service(Trigger, f'~/{name}', self.wrap(name), callback_group=group)
        self.get_logger().info(f'ready, stack: {self.repo}')

    # ------------------------------------------------------------ settings
    def read_config(self):
        def grab(path, pattern):
            try:
                m = re.search(pattern, open(path).read(), re.M)
                return m.group(1) if m else ''
            except OSError:
                return ''
        tui = {k: grab(self.tuienv, rf'^{k}=(.*)$') for k in ('DEPTH', 'LIDAR_Z', 'GUI', 'CLOUDINI')}
        return {
            'mid360_ip': grab(self.livox, r'"ip"\s*:\s*"([0-9.]+)"'),
            'orin_lidar_ip': grab(self.livox, r'"cmd_data_ip"\s*:\s*"([0-9.]+)"'),
            'ros_domain': grab(self.netenv, r'^ROS_DOMAIN_ID=(.*)$') or '0',
            'laptop_peer': grab(self.netenv, r'^ROS_STATIC_PEERS=(.*)$'),
            'depth': tui['DEPTH'] or 'zed',
            'lidar_z': tui['LIDAR_Z'] or '0.60',
            'gui': tui['GUI'] or 'auto',
            'cloudini': (tui['CLOUDINI'] or 'false') == 'true',
        }

    def validate(self, params):
        for p in params:
            v = p.value
            if p.name in ('mid360_ip', 'orin_lidar_ip') and not IP_RE.match(str(v)):
                return SetParametersResult(successful=False, reason=f'{p.name}: "{v}" is not an IP')
            if p.name == 'laptop_peer' and str(v) and not IP_RE.match(str(v)):
                return SetParametersResult(successful=False, reason=f'laptop_peer: "{v}" is not an IP (empty = multicast)')
            if p.name in CHOICES and str(v) not in CHOICES[p.name]:
                return SetParametersResult(successful=False, reason=f'{p.name} must be one of {CHOICES[p.name]}')
            if p.name == 'lidar_z' and not re.match(r'^[0-9]+(\.[0-9]+)?$', str(v)):
                return SetParametersResult(successful=False, reason=f'lidar_z: "{v}" is not a height in m')
        return SetParametersResult(successful=True)

    def p(self, name):
        v = self.get_parameter(name).value
        return v if isinstance(v, bool) else str(v).strip()

    def write_config(self):
        """Same edits as start_tui.sh's set_lidar_ip / set_host_ip / set_netvar / save."""
        lip, hip, peer = self.p('mid360_ip'), self.p('orin_lidar_ip'), self.p('laptop_peer')
        s = open(self.livox).read()
        s = re.sub(r'("ip"\s*:\s*")[0-9.]+', rf'\g<1>{lip}', s)
        s = re.sub(r'("(cmd_data|push_msg|point_data|imu_data)_ip"\s*:\s*")[0-9.]+', rf'\g<1>{hip}', s)
        open(self.livox, 'w').write(s)
        n = open(self.netenv).read()
        line = f'ROS_STATIC_PEERS={peer}' if peer else '# ROS_STATIC_PEERS=192.168.1.20'
        if re.search(r'^#? *ROS_STATIC_PEERS=.*$', n, re.M):
            n = re.sub(r'^#? *ROS_STATIC_PEERS=.*$', line, n, count=1, flags=re.M)
        else:
            n += line + '\n'
        open(self.netenv, 'w').write(n)
        os.makedirs(os.path.dirname(self.tuienv), exist_ok=True)
        cl = 'true' if self.p('cloudini') in (True, 'true', 'True') else 'false'
        open(self.tuienv, 'w').write(f'DEPTH={self.p("depth")}\nLIDAR_Z={self.p("lidar_z")}\n'
                                     f'GUI={self.p("gui")}\nCLOUDINI={cl}\n')

    def launch_args(self):
        a = f'depth:={self.p("depth")} lidar_z:={self.p("lidar_z")}'
        if self.p('gui') != 'auto':
            a += f' gui:={self.p("gui")}'
        if self.p('cloudini') in (True, 'true', 'True'):
            a += ' cloudini:=true'
        return a

    # ------------------------------------------------------------ state
    def container_up(self):
        return sh(f"docker ps --format '{{{{.Names}}}}' | grep -qx {NAME}")[0] == 0

    def stack_up(self):
        return self.container_up() and sh(f"docker exec {NAME} pgrep -f '[r]os2 launch kratos_bringup'")[0] == 0

    # ------------------------------------------------------------ services
    def wrap(self, name):
        fn = getattr(self, 'do_' + name)

        def cb(req, res):
            try:
                ok, msg = fn()
            except Exception as e:   # a bug here must reach the GUI, not kill the node
                ok, msg = False, f'{name} crashed on the Orin: {type(e).__name__}: {e}'
                self.get_logger().error(msg)
            res.success, res.message = ok, strip_ansi(msg)
            return res
        return cb

    def exclusive(self, what, fn):
        if not self.busy.acquire(blocking=False):
            return False, f'busy with another start/diagnose/health, try {what} again in a moment'
        try:
            return fn()
        finally:
            self.busy.release()

    def do_start(self):
        def run():
            if self.stack_up():
                return False, 'the stack is already running (STOP first)'
            self.write_config()
            args = self.launch_args()
            self.get_logger().info(f'start.sh --no-follow {args}')
            # The first start after a fresh clone builds the workspace: minutes
            code, out = sh(f'cd {self.repo} && ./start.sh --no-follow {args}', timeout=900)
            # start.sh returns before the launch shows up in the container
            ok = False
            for _ in range(20):
                if code != 0 or (ok := self.stack_up()):
                    break
                time.sleep(1)
            return ok, f'start.sh --no-follow {args}\n{out}' + (
                '\nKEEP THE ROVER STILL ~10 s (GLIM IMU init). HEALTH runs by itself after that.' if ok else '')
        return self.exclusive('START', run)

    def do_stop(self):
        # Not exclusive: STOP must always get through
        code, out = sh(f'cd {self.repo} && ./stop.sh', timeout=150)
        return code == 0, out or 'stop.sh printed nothing'

    def do_diagnose(self):
        return self.exclusive('DIAGNOSE', self.diagnose)

    def diagnose(self):
        lines, fails, warns = [], 0, 0

        def item(ok, label, detail, warn=False):
            nonlocal fails, warns
            mark = 'OK  ' if ok else ('WARN' if warn else 'FAIL')
            fails += (not ok and not warn)
            warns += (not ok and warn)
            lines.append(f'[{mark}] {label}: {detail}')

        cfg = self.read_config()
        if sh('docker info')[0] != 0:
            item(False, 'docker', f'not reachable (is {os.environ.get("USER")} in group docker?)')
        elif sh('docker image inspect kratos/glim_nvblox:latest')[0] != 0:
            item(False, 'docker', 'image kratos/glim_nvblox:latest missing: docker/build_image.sh')
        else:
            item(True, 'docker', 'kratos/glim_nvblox:latest')

        built = os.path.islink(f'{self.repo}/install/kratos_bringup/share/kratos_bringup/launch/kratos.launch.py')
        item(built, 'workspace built', 'install/' if built else
             'install/ has no kratos_bringup (the first START builds it: several minutes)')

        lip, hip = cfg['mid360_ip'], cfg['orin_lidar_ip']
        prefix = lip.rsplit('.', 1)[0] + '.'
        _, addrs = sh('ip -4 -o addr')
        iface = cur = ''
        for l in addrs.splitlines():
            f = l.split()
            if len(f) > 3 and f[3].startswith(prefix):
                iface, cur = f[1], f[3].split('/')[0]
                break
        if not iface:
            item(False, f'Orin LiDAR port ({hip})', f'no interface in {prefix}0/24. Interfaces:\n      '
                 + sh('ip -br -4 addr | grep -v ^lo')[1].replace('\n', '\n      '))
        elif cur == hip or sh(f'ip -4 -o addr show dev {iface} | grep -q " {hip}/"')[0] == 0:
            item(True, f'Orin LiDAR port ({hip})', f'{iface} has {hip}')
        else:
            item(False, f'Orin LiDAR port ({hip})', f'{iface} is {cur}, MID360_config.json sends to {hip}. '
                 f'Set "Orin LiDAR IP" to {cur}')

        if sh(f'ping -c 2 -W 1 {lip}')[0] == 0:
            item(True, f'MID-360 ping ({lip})', 'answers')
        else:
            seen = sh(f"ip neigh show | awk -v p='{prefix}' 'index($1,p)==1 && $1!=\"{hip}\" && "
                      "/REACHABLE|STALE|DELAY|PROBE/ {print $1}' | tr '\\n' ' '")[1]
            item(False, f'MID-360 ping ({lip})', 'no answer. Check power + cable; its default IP is '
                 '192.168.1.1XX (XX = last 2 digits of the serial)' + (f'. Seen on the link: {seen}' if seen else ''))

        if cfg['depth'] == 'none':
            item(True, 'ZED 2i', 'skipped (depth none)')
        elif self.stack_up():
            item(True, 'ZED 2i', 'in use by this stack')
        else:
            usb = sh("lsusb | grep -qi '2b03:f880'")[0] == 0
            item(usb, 'ZED 2i on USB', '2b03:f880' if usb else 'not found (USB 3 port, cable), or set Depth to none')
            _, names = sh(f"docker ps --format '{{{{.Names}}}}' | grep -vx {NAME}")
            holders = [c for c in names.split() if sh(
                f"docker top {c} -eo pid,args | grep -qE 'zed_wrapper|zed_node|component_container.*zed'")[0] == 0]
            item(not holders, 'ZED free', 'nobody else holds it' if not holders else
                 f'held by container {" ".join(holders)}: stop it, or set Depth to none')

        peer = cfg['laptop_peer']
        if self.container_up():
            _, env = sh(f'docker exec {NAME} bash -c \'echo "$ROS_DOMAIN_ID/$ROS_STATIC_PEERS"\'')
            if env != f'{cfg["ros_domain"]}/{peer}':
                item(False, 'ROS network', f'container runs an old ros_network.env ({env}); STOP + START applies it', warn=True)
            else:
                item(True, 'ROS network', f'domain {cfg["ros_domain"]}, {peer or "multicast discovery"}')
        elif peer and sh(f'ping -c 1 -W 1 {peer}')[0] != 0:
            item(False, 'ROS network', f'laptop peer {peer} not answering', warn=True)
        else:
            item(True, 'ROS network', f'domain {cfg["ros_domain"]}, {peer or "multicast discovery"}')

        z = cfg['lidar_z']
        item(z == '0.60', 'lidar_z', '0.60 (matches nav2_params / pcd2pgm_live)' if z == '0.60' else
             f'{z}: also update the nav2_params.yaml and pcd2pgm_live.yaml heights', warn=True)

        lines.append(f'{len(lines)} checks: {fails} failed, {warns} warnings')
        return fails == 0, '\n'.join(lines)

    def do_health(self):
        return self.exclusive('HEALTH', self.health)

    def health(self):
        if not self.stack_up():
            return False, 'stack not running'
        depth = self.read_config()['depth']
        topics = ['/livox/lidar', '/livox/imu', '/livox/lidar_filtered', '/glim_ros/odom', '/glim_ros/map',
                  '/map', '/local_costmap/costmap']
        if depth != 'none':
            topics += ['/zed/zed_node/rgb/color/rect/image', '/nvblox_node/static_esdf_pointcloud', '/nvblox_node/mesh']
        if depth == 'ess':
            topics.append('/ess/depth')
        inner = ('for t in ' + ' '.join(topics) + '; do (timeout 15 ros2 topic echo --once --no-arr $t '
                 '>/dev/null 2>&1 && echo ok $t || echo no $t) & done; wait')
        _, out = sh(f'docker exec -u {os.getuid()}:{os.getgid()} -w /workspaces/kratos_glim {NAME} '
                    f"bash -lc '{inner}'", timeout=40)
        got = dict(reversed(l.split(None, 1)) for l in out.splitlines() if l.startswith(('ok ', 'no ')))
        lines = [f'[{"OK  " if got.get(t) == "ok" else "FAIL"}] {t}' for t in topics]
        fails = sum(got.get(t) != 'ok' for t in topics)
        lines.append(f'{len(topics)} topics: {fails} silent')
        if fails:
            _, errs = sh(f"grep -aE '\\[(ERROR|FATAL)\\]|[Ee]xception' {self.repo}/log/bringup/latest.log | tail -8")
            if errs:
                lines += ['last errors in the log:'] + ['  ' + e[:200] for e in errs.splitlines()]
        return fails == 0, '\n'.join(lines)

    def do_log(self):
        path = f'{self.repo}/log/bringup/latest.log'
        if not os.path.exists(path):
            return False, 'no bring-up log yet (START first)'
        pat = self.p('log_filter')
        try:
            text = open(path, errors='replace').read().splitlines()
        except OSError as e:
            return False, str(e)
        if pat:
            try:
                rx = re.compile(pat)
            except re.error as e:
                return False, f'bad log filter "{pat}": {e}'
            text = [l for l in text if rx.search(l)]
        return True, f'{os.path.realpath(path)}' + (f'  (filter: {pat})' if pat else '') + '\n' + '\n'.join(text[-80:])

    def do_status(self):
        c, s = self.container_up(), self.stack_up()
        msg = f'container={"up" if c else "down"}  stack={"running" if s else "stopped"}'
        try:
            started = int(open(f'{self.repo}/log/bringup/started_at').read())
            if s:
                msg += f'  uptime={int(time.time()) - started} s'
        except (OSError, ValueError):
            pass
        return True, msg

    def do_get_config(self):
        cfg = self.read_config()
        cfg['cloudini'] = 'true' if cfg['cloudini'] else 'false'
        return True, '\n'.join(f'{k}={v}' for k, v in cfg.items())

    def do_rviz(self):
        if not self.stack_up():
            return False, 'stack not running'
        auth = sorted(glob.glob(f'/run/user/{os.getuid()}/.mutter-Xwaylandauth.*') +
                      glob.glob(f'/run/user/{os.getuid()}/gdm/Xauthority'))
        if not auth:
            return False, ('nobody is logged in on the Orin\'s own screen. The GUI\'s MAP tab is the '
                           'laptop RViz; it needs the same ROS_DOMAIN_ID')
        code, out = sh(f'docker exec -d -u {os.getuid()}:{os.getgid()} -e DISPLAY=:0 -e XAUTHORITY={auth[-1]} '
                       f'-w /workspaces/kratos_glim {NAME} bash -lc '
                       '"exec rviz2 -d install/kratos_bringup/share/kratos_bringup/rviz/kratos.rviz"')
        return code == 0, 'RViz started on the Orin\'s screen (:0)' if code == 0 else out


def main():
    rclpy.init()
    node = SystemControl()
    ex = MultiThreadedExecutor(num_threads=6)
    ex.add_node(node)
    try:
        ex.spin()
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
