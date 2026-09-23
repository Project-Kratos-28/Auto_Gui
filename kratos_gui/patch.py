import sys

fpath = '/home/zerobx0/ros2_ws/src/kratos-rover/kratos_cameras/src/gst_stream_manager.cpp'
with open(fpath, 'r') as f:
    content = f.read()

target = """std::string GstStreamManager::build_pipeline_description(
    const GstCameraPipelineConfig &config) const {
  std::ostringstream ss;
  ss << "v4l2src device=" << config.camera_device << " ! "
     << "image/jpeg,width=" << config.width << ",height=" << config.height
     << ",framerate=" << config.fps << "/1 ! "
     << "nvv4l2decoder mjpeg=1 ! "
     << "nvvidconv ! "
     << "video/x-raw(memory:NVMM),format=NV12 ! ";"""

replacement = """std::string GstStreamManager::build_pipeline_description(
    const GstCameraPipelineConfig &config) const {
  std::ostringstream ss;
  ss << "v4l2src device=" << config.camera_device << " ! ";
  
  if (config.format == "MJPG") {
      ss << "image/jpeg,width=" << config.width << ",height=" << config.height
         << ",framerate=" << config.fps << "/1 ! "
         << "nvv4l2decoder mjpeg=1 ! "
         << "nvvidconv ! "
         << "video/x-raw(memory:NVMM),format=NV12 ! ";
  } else {
      ss << "video/x-raw,format=" << config.format << ",width=" << config.width << ",height=" << config.height
         << ",framerate=" << config.fps << "/1 ! "
         << "nvvidconv ! "
         << "video/x-raw(memory:NVMM),format=NV12 ! ";
  }"""

if target in content:
    content = content.replace(target, replacement)
    with open(fpath, 'w') as f:
        f.write(content)
    print("Patched gst_stream_manager.cpp")
else:
    print("Could not find target content in gst_stream_manager.cpp")
    sys.exit(1)
