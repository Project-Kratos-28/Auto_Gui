cmake_minimum_required(VERSION 3.8)
project(kratos_cloudini)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(cloudini_lib REQUIRED)

add_executable(cloudini_decoder src/cloudini_decoder.cpp)
target_link_libraries(cloudini_decoder cloudini_lib::cloudini_lib)
ament_target_dependencies(cloudini_decoder rclcpp)

install(TARGETS cloudini_decoder
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
