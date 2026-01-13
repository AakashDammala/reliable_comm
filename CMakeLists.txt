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
find_package(geometry_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

# Generate custom messages
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/ReliableEnvelope.msg"
  "msg/ReliableFeedbackV2.msg"
)

# Export message runtime dependency
ament_export_dependencies(rosidl_default_runtime)

include_directories(include ${catkin_INCLUDE_DIRS})

add_executable(regular_pub src/regular_pub.cpp)
target_include_directories(regular_pub PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
ament_target_dependencies(regular_pub rclcpp std_msgs sensor_msgs geometry_msgs)
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(regular_pub "${cpp_typesupport_target}")

add_executable(regular_sub src/regular_sub.cpp)
target_include_directories(regular_sub PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
ament_target_dependencies(regular_sub rclcpp std_msgs sensor_msgs geometry_msgs)
target_link_libraries(regular_sub "${cpp_typesupport_target}")

install(TARGETS
  regular_pub
  regular_sub
  DESTINATION lib/${PROJECT_NAME})

# Install header files so other packages can use them
install(DIRECTORY include/
  DESTINATION include)

# Export include directories for downstream packages
ament_export_include_directories(include)

# Export dependencies that downstream packages will need
ament_export_dependencies(rclcpp std_msgs)

ament_package()