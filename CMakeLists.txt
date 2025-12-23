cmake_minimum_required(VERSION 3.5)
project(reliable_comm)

# Default to C++14
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

include_directories(include ${catkin_INCLUDE_DIRS})

add_executable(regular_pub src/regular_pub.cpp)
target_include_directories(regular_pub PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
ament_target_dependencies(regular_pub rclcpp std_msgs sensor_msgs)

add_executable(regular_sub src/regular_sub.cpp)
target_include_directories(regular_sub PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
ament_target_dependencies(regular_sub rclcpp std_msgs sensor_msgs)

install(TARGETS
  regular_pub
  regular_sub
  DESTINATION lib/${PROJECT_NAME})

ament_package()