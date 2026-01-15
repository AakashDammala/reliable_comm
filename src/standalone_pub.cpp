/**
 * standalone_pub.cpp - Standalone Reliable Publisher Node
 * A thin wrapper that hosts ReliablePub<T> as a ROS2 executable.
 * Supported message types: cdcl_umd_msgs/Observation.msg, sensor_msgs/Image,
 * geometry_msgs/TwistStamped
 */

#include <algorithm>
#include <memory>
#include <string>

#include "cdcl_umd_msgs/msg/observation.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "reliable_comm/reliable_pub_v2.hpp"
#include "sensor_msgs/msg/image.hpp"

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);

  // Create a temporary node just to read parameters
  auto param_node = std::make_shared<rclcpp::Node>("standalone_pub_params");

  // Declare parameters with defaults
  param_node->declare_parameter<std::string>("topic_name", "/observation");
  param_node->declare_parameter<std::string>("message_type",
                                             "cdcl_umd_msgs/Observation");
  param_node->declare_parameter<int>("qos_history_depth", 1000);
  param_node->declare_parameter<int>("max_retries_per_loop", 25);
  param_node->declare_parameter<double>("retry_rate_hz", 1.0);
  param_node->declare_parameter<int>("max_storage_size", 1000);

  // Get parameters
  std::string topic_name = param_node->get_parameter("topic_name").as_string();
  std::string message_type =
      param_node->get_parameter("message_type").as_string();
  size_t qos_history_depth = static_cast<size_t>(
      param_node->get_parameter("qos_history_depth").as_int());
  size_t max_retries_per_loop = static_cast<size_t>(
      param_node->get_parameter("max_retries_per_loop").as_int());
  double retry_rate_hz = param_node->get_parameter("retry_rate_hz").as_double();
  size_t max_storage_size = static_cast<size_t>(
      param_node->get_parameter("max_storage_size").as_int());

  RCLCPP_INFO(param_node->get_logger(),
              "Starting standalone_pub: topic=%s, type=%s, qos=%zu, "
              "retries=%zu, rate=%.1fHz, storage=%zu",
              topic_name.c_str(),
              message_type.c_str(),
              qos_history_depth,
              max_retries_per_loop,
              retry_rate_hz,
              max_storage_size);

  // Use multi-threaded executor for concurrent callback handling
  rclcpp::executors::MultiThreadedExecutor executor;

  // Generate node name from topic (replace '/' with '_')
  std::string sanitized_topic = topic_name;
  std::replace(sanitized_topic.begin(), sanitized_topic.end(), '/', '_');
  std::string node_name = "reliable_pub_" + sanitized_topic;

  // Release param node - no longer needed after reading parameters
  param_node.reset();

  // Runtime dispatch based on message type
  if (message_type == "sensor_msgs/Image")
  {
    auto reliable_pub_node =
        std::make_shared<ReliablePub<sensor_msgs::msg::Image>>(
            node_name,
            topic_name,
            qos_history_depth,
            max_retries_per_loop,
            retry_rate_hz,
            max_storage_size);
    executor.add_node(reliable_pub_node);
    executor.spin();
  }
  else if (message_type == "geometry_msgs/TwistStamped")
  {
    auto reliable_pub_node =
        std::make_shared<ReliablePub<geometry_msgs::msg::TwistStamped>>(
            node_name,
            topic_name,
            qos_history_depth,
            max_retries_per_loop,
            retry_rate_hz,
            max_storage_size);
    executor.add_node(reliable_pub_node);
    executor.spin();
  }
  else if (message_type == "cdcl_umd_msgs/Observation")
  {
    auto reliable_pub_node =
        std::make_shared<ReliablePub<cdcl_umd_msgs::msg::Observation>>(
            node_name,
            topic_name,
            qos_history_depth,
            max_retries_per_loop,
            retry_rate_hz,
            max_storage_size);
    executor.add_node(reliable_pub_node);
    executor.spin();
  }
  else
  {
    RCLCPP_ERROR(rclcpp::get_logger(node_name),
                 "Unsupported message type: %s. Supported: sensor_msgs/Image, "
                 "geometry_msgs/TwistStamped, cdcl_umd_msgs/Observation",
                 message_type.c_str());
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::shutdown();
  return 0;
}
