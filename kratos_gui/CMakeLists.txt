cmake_minimum_required(VERSION 3.8)
project(kratos_gui)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Qt auto-processing ──────────────────────────────────────────────
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Dependencies ────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(kratos_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED
  gstreamer-1.0
  gstreamer-video-1.0
  gstreamer-app-1.0
)

# ── Executable ──────────────────────────────────────────────────────
add_executable(kratos_gui
  src/main.cpp
  src/mainwindow.cpp
  src/mainwindow.h
  src/mainwindow.ui
  src/ros_worker.cpp
  src/ros_worker.h
  src/video_worker.cpp
  src/video_worker.h
  src/camerawidget.cpp
  src/camerawidget.h
  src/camerawidget.ui
  src/joynodewidget.cpp
  src/joynodewidget.h
  src/joynodewidget.ui
  src/thrustpillarwidget.cpp
  src/thrustpillarwidget.h
  src/vectorradarwidget.cpp
  src/vectorradarwidget.h
  src/videoplayerwidget.cpp
  src/videoplayerwidget.h
)

target_include_directories(kratos_gui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(kratos_gui
  Qt6::Widgets
  ${GST_LIBRARIES}
)

ament_target_dependencies(kratos_gui
  rclcpp
  kratos_msgs
  sensor_msgs
)

install(TARGETS kratos_gui
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
