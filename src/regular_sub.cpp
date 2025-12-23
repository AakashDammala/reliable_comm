#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "reliable_comm/reliable_sub.hpp"
#include "sensor_msgs/msg/image.hpp"

typedef sensor_msgs::msg::Image msg_type;

class RegularSub : public rclcpp::Node
{
 public:
  RegularSub() : Node("regular_sub")
  {
    subscription_ = this->create_subscription<msg_type>(
        "/rs_image",
        10,
        std::bind(&RegularSub::topic_callback, this, std::placeholders::_1));
  }

 private:
  void topic_callback(const msg_type::ConstSharedPtr msg_ptr) const
  {
    RCLCPP_INFO(this->get_logger(), "Count of image: '%d'", msg_ptr->data[0]);
  }

  rclcpp::Subscription<msg_type>::SharedPtr subscription_;
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
  executor.add_node(reliable_sub_node);
  executor.spin();

  rclcpp::shutdown();
  return 0;
}