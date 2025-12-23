#include <chrono>
#include <functional>
#include <memory>
#include <rclcpp/time.hpp>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "reliable_comm/reliable_pub.hpp"
#include "sensor_msgs/msg/image.hpp"

using namespace std::chrono_literals;

typedef sensor_msgs::msg::Image msg_type;

class RegularPub : public rclcpp::Node
{
 public:
  RegularPub() : Node("regular_pub"), count_(0)
  {
    publisher_ = this->create_publisher<msg_type>("/image", 10);
    timer_ = this->create_wall_timer(
        2s, std::bind(&RegularPub::timer_callback, this));
  }

 private:
  void timer_callback()
  {
    auto msg = sensor_msgs::msg::Image();
    msg.data.push_back(count_);
    msg.header.stamp = this->now();
    count_++;
    publisher_->publish(msg);
  }

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<msg_type>::SharedPtr publisher_;
  uint32_t count_;
};

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::Node::SharedPtr main_node = std::make_shared<RegularPub>();

  std::string node_name = "reliable_pub";
  std::string input_topic_name = "/image";
  rclcpp::Node::SharedPtr reliable_pub_node =
      std::make_shared<ReliablePub<sensor_msgs::msg::Image>>(node_name,
                                                             input_topic_name);

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(main_node);
  executor.add_node(reliable_pub_node);
  executor.spin();

  rclcpp::shutdown();
  return 0;
}
