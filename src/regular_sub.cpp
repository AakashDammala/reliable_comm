#include <fstream>
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
    this->declare_parameter<int>("expected_messages", 1000);
    this->declare_parameter<std::string>("output_file", "");
    this->declare_parameter<int>("timeout_sec", 120);

    expected_messages_ = this->get_parameter("expected_messages").as_int();
    output_file_ = this->get_parameter("output_file").as_string();
    int timeout_sec = this->get_parameter("timeout_sec").as_int();

    // BEST_EFFORT QoS with large buffer to match publisher
    auto qos = rclcpp::QoS(1000).best_effort();

    // Subscribe to /rs_image (output of ReliableSub, which is reliable)
    image_subscription_ = this->create_subscription<ImageMsg>(
        "/rs_image",
        qos,
        std::bind(&RegularSub::image_callback, this, std::placeholders::_1));

    twist_subscription_ = this->create_subscription<TwistMsg>(
        "/twist",
        qos,
        std::bind(&RegularSub::twist_callback, this, std::placeholders::_1));

    // Timeout timer
    timeout_timer_ =
        this->create_wall_timer(std::chrono::seconds(timeout_sec),
                                std::bind(&RegularSub::on_timeout, this));

    RCLCPP_INFO(this->get_logger(),
                "Expecting %d messages, timeout %ds",
                expected_messages_,
                timeout_sec);
  }

 private:
  void image_callback(const ImageMsg::ConstSharedPtr /* msg_ptr */)
  {
    image_received_count_++;
    if (image_received_count_ % 100 == 0)
    {
      RCLCPP_INFO(
          this->get_logger(), "Received %d images", image_received_count_);
    }
    if (image_received_count_ >= expected_messages_)
    {
      write_stats();
      // rclcpp::shutdown();
    }
  }

  void twist_callback(const TwistMsg::ConstSharedPtr /* msg_ptr */)
  {
    twist_received_count_++;
    if (twist_received_count_ % 100 == 0)
    {
      RCLCPP_INFO(
          this->get_logger(), "Received %d twists", twist_received_count_);
    }
  }

  void on_timeout()
  {
    RCLCPP_WARN(this->get_logger(), "Timeout reached, writing stats");
    write_stats();
    rclcpp::shutdown();
  }

  void write_stats()
  {
    if (output_file_.empty())
      return;
    std::ofstream f(output_file_);
    double loss_rate = 1.0 - (double)image_received_count_ / expected_messages_;
    f << "{\"expected\": " << expected_messages_
      << ", \"received\": " << image_received_count_
      << ", \"loss_rate\": " << loss_rate << "}";
    f.close();
    RCLCPP_INFO(this->get_logger(),
                "Stats written: %d/%d (%.1f%% loss)",
                image_received_count_,
                expected_messages_,
                loss_rate * 100);
  }

  rclcpp::Subscription<ImageMsg>::SharedPtr image_subscription_;
  rclcpp::Subscription<TwistMsg>::SharedPtr twist_subscription_;
  rclcpp::TimerBase::SharedPtr timeout_timer_;
  int image_received_count_;
  int twist_received_count_;
  int expected_messages_;
  std::string output_file_;
};

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::Node::SharedPtr main_node = std::make_shared<RegularSub>();

  std::string node_name = "reliable_sub";
  std::string input_topic_name =
      "/image";  // ReliableSub subscribes to /rp_image, publishes to /rs_image
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