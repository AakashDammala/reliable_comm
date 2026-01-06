#include <memory>

#include "geometry_msgs/msg/twist_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "reliable_comm/reliable_sub.hpp"
#include "sensor_msgs/msg/image.hpp"

typedef sensor_msgs::msg::Image ImageMsg;
typedef geometry_msgs::msg::TwistStamped TwistMsg;

class RegularSub : public rclcpp::Node
{
 public:
  RegularSub()
      : Node("regular_sub"), image_received_count_(0), twist_received_count_(0)
  {
    image_subscription_ = this->create_subscription<ImageMsg>(
        "/image",
        10,
        std::bind(&RegularSub::image_callback, this, std::placeholders::_1));

    twist_subscription_ = this->create_subscription<TwistMsg>(
        "/twist",
        10,
        std::bind(&RegularSub::twist_callback, this, std::placeholders::_1));
  }

 private:
  void image_callback(const ImageMsg::ConstSharedPtr msg_ptr)
  {
    image_received_count_++;

    // Extract message number from frame_id
    std::string msg_number = msg_ptr->header.frame_id;

    // Get timestamp from header
    auto stamp = msg_ptr->header.stamp;
    double timestamp_sec = stamp.sec + stamp.nanosec / 1e9;

    RCLCPP_INFO(this->get_logger(),
                "[Image] Received #%d, msg_num: %s, timestamp: %.6f",
                image_received_count_,
                msg_number.c_str(),
                timestamp_sec);
  }

  void twist_callback(const TwistMsg::ConstSharedPtr msg_ptr)
  {
    twist_received_count_++;

    // Extract message number from frame_id
    std::string msg_number = msg_ptr->header.frame_id;

    // Get timestamp from header
    auto stamp = msg_ptr->header.stamp;
    double timestamp_sec = stamp.sec + stamp.nanosec / 1e9;

    RCLCPP_INFO(this->get_logger(),
                "[Twist] Received #%d, msg_num: %s, timestamp: %.6f",
                twist_received_count_,
                msg_number.c_str(),
                timestamp_sec);
  }

  rclcpp::Subscription<ImageMsg>::SharedPtr image_subscription_;
  rclcpp::Subscription<TwistMsg>::SharedPtr twist_subscription_;
  int image_received_count_;
  int twist_received_count_;
};

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::Node::SharedPtr main_node = std::make_shared<RegularSub>();

  std::string node_name = "reliable_sub";
  std::string input_topic_name = "/image";
  rclcpp::Node::SharedPtr reliable_sub_node =
      std::make_shared<ReliableSub<sensor_msgs::msg::Image>>(node_name,
                                                             input_topic_name);

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(main_node);
  // executor.add_node(reliable_sub_node);
  executor.spin();

  rclcpp::shutdown();
  return 0;
}