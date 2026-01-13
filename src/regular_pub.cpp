#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>
#include <string>

#include "geometry_msgs/msg/twist_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "reliable_comm/reliable_pub.hpp"
#include "sensor_msgs/msg/image.hpp"

using namespace std::chrono_literals;

typedef sensor_msgs::msg::Image ImageMsg;
typedef geometry_msgs::msg::TwistStamped TwistMsg;

class RegularPub : public rclcpp::Node
{
 public:
  RegularPub() : Node("regular_pub"), image_msg_count_(0), twist_msg_count_(0)
  {
    // Declare image parameters with defaults
    this->declare_parameter<int>("image_publish_frequency", 30);
    this->declare_parameter<std::string>("image_size", "720p");
    this->declare_parameter<int>("image_num_messages", 500);

    // Declare twist parameters with defaults
    this->declare_parameter<int>("twist_publish_frequency", 30);
    this->declare_parameter<int>("twist_num_messages", 0);

    // Output file for stats
    this->declare_parameter<std::string>("output_file", "");
    this->declare_parameter<int>("timeout_sec", 120);
    output_file_ = this->get_parameter("output_file").as_string();
    int timeout_sec = this->get_parameter("timeout_sec").as_int();

    auto qos = rclcpp::QoS(1000).best_effort();
    image_publisher_ = this->create_publisher<ImageMsg>("/image", qos);
    twist_publisher_ = this->create_publisher<TwistMsg>("/twist", qos);

    // Get image parameters
    int image_frequency =
        this->get_parameter("image_publish_frequency").as_int();
    std::string size = this->get_parameter("image_size").as_string();
    image_num_messages_ = this->get_parameter("image_num_messages").as_int();

    // Get twist parameters
    int twist_frequency =
        this->get_parameter("twist_publish_frequency").as_int();
    twist_num_messages_ = this->get_parameter("twist_num_messages").as_int();

    // Set image dimensions based on size parameter
    if (size == "1080p")
    {
      width_ = 1920;
      height_ = 1080;
    }
    else if (size == "720p")
    {
      width_ = 1280;
      height_ = 720;
    }
    else if (size == "480p")
    {
      width_ = 640;
      height_ = 480;
    }
    else if (size == "240p")
    {
      width_ = 320;
      height_ = 240;
    }
    else if (size == "120p")
    {
      width_ = 160;
      height_ = 120;
    }
    else if (size == "96p")
    {
      width_ = 96;
      height_ = 96;
    }
    else if (size == "64p")
    {
      width_ = 64;
      height_ = 64;
    }
    else
    {
      width_ = 32;
      height_ = 32;
    }

    // Startup delay to allow tc rules to be applied before publishing
    this->declare_parameter<int>("startup_delay_sec", 4);
    int startup_delay = this->get_parameter("startup_delay_sec").as_int();
    RCLCPP_INFO(this->get_logger(),
                "Waiting %d seconds before starting publishers...",
                startup_delay);
    rclcpp::sleep_for(std::chrono::seconds(startup_delay));

    // Start image timer only if frequency > 0
    if (image_frequency > 0)
    {
      auto image_period_ms = std::chrono::milliseconds(1000 / image_frequency);
      RCLCPP_INFO(this->get_logger(),
                  "Starting image publisher: %d Hz, %s (%dx%d), %d messages",
                  image_frequency,
                  size.c_str(),
                  width_,
                  height_,
                  image_num_messages_);
      image_timer_ = this->create_wall_timer(
          image_period_ms,
          std::bind(&RegularPub::generate_publish_image, this));
    }
    else
    {
      RCLCPP_INFO(this->get_logger(),
                  "Image publisher disabled (frequency <= 0)");
    }

    // Start twist timer only if frequency > 0
    if (twist_frequency > 0)
    {
      auto twist_period_ms = std::chrono::milliseconds(1000 / twist_frequency);
      RCLCPP_INFO(this->get_logger(),
                  "Starting twist publisher: %d Hz, %d messages",
                  twist_frequency,
                  twist_num_messages_);
      twist_timer_ = this->create_wall_timer(
          twist_period_ms,
          std::bind(&RegularPub::generate_publish_twist, this));
    }
    else
    {
      RCLCPP_INFO(this->get_logger(),
                  "Twist publisher disabled (frequency <= 0)");
    }

    // Shutdown timer
    shutdown_timer_ =
        this->create_wall_timer(std::chrono::seconds(timeout_sec),
                                std::bind(&RegularPub::on_timeout, this));
    RCLCPP_INFO(
        this->get_logger(), "Will shutdown after %d seconds", timeout_sec);
  }

 private:
  void generate_publish_image()
  {
    if (image_msg_count_ >= image_num_messages_)
    {
      RCLCPP_INFO(this->get_logger(),
                  "Published all %d image messages, stopping.",
                  image_num_messages_);
      image_timer_->cancel();
      write_stats();
      // rclcpp::shutdown();
      return;
    }

    auto msg = sensor_msgs::msg::Image();

    // Set header with timestamp
    msg.header.stamp = this->now();
    msg.header.frame_id = std::to_string(image_msg_count_);

    // Set image metadata
    msg.width = width_;
    msg.height = height_;
    msg.encoding = "rgb8";
    msg.is_bigendian = false;
    msg.step = width_ * 3;  // 3 bytes per pixel (RGB)

    // Generate random image data
    size_t data_size = height_ * msg.step;
    msg.data.resize(data_size);

    // Fill with random data
    // std::random_device rd;
    // std::mt19937 gen(rd());
    // std::uniform_int_distribution<uint8_t> dist(0, 255);
    // for (size_t i = 0; i < data_size; ++i)
    // {
    //   msg.data[i] = dist(gen);
    // }

    RCLCPP_INFO(
        this->get_logger(), "Publishing image message %d", image_msg_count_);
    image_publisher_->publish(msg);
    image_msg_count_++;

    if (image_msg_count_ % 1000 == 0)
    {
      RCLCPP_INFO(
          this->get_logger(), "Published %d image messages", image_msg_count_);
    }
  }

  void generate_publish_twist()
  {
    if (twist_msg_count_ >= twist_num_messages_)
    {
      RCLCPP_INFO(this->get_logger(),
                  "Published all %d twist messages, stopping.",
                  twist_num_messages_);
      twist_timer_->cancel();
      write_stats();
      return;
    }

    auto msg = geometry_msgs::msg::TwistStamped();

    // Set header with timestamp
    msg.header.stamp = this->now();
    msg.header.frame_id = std::to_string(twist_msg_count_);

    // Set twist values (random or fixed values)
    msg.twist.linear.x = 1.0;
    msg.twist.linear.y = 0.0;
    msg.twist.linear.z = 0.0;
    msg.twist.angular.x = 0.0;
    msg.twist.angular.y = 0.0;
    msg.twist.angular.z = 0.5;

    twist_publisher_->publish(msg);
    twist_msg_count_++;

    if (twist_msg_count_ % 1000 == 0)
    {
      RCLCPP_INFO(
          this->get_logger(), "Published %d twist messages", twist_msg_count_);
    }
  }

  // Image members
  rclcpp::TimerBase::SharedPtr image_timer_;
  rclcpp::Publisher<ImageMsg>::SharedPtr image_publisher_;
  int image_msg_count_;
  int image_num_messages_;
  uint32_t width_;
  uint32_t height_;

  // Twist members
  rclcpp::TimerBase::SharedPtr twist_timer_;
  rclcpp::Publisher<TwistMsg>::SharedPtr twist_publisher_;
  int twist_msg_count_;
  int twist_num_messages_;
  std::string output_file_;
  rclcpp::TimerBase::SharedPtr shutdown_timer_;

  void on_timeout()
  {
    RCLCPP_INFO(this->get_logger(), "Timeout reached, shutting down");
    write_stats();
    rclcpp::shutdown();
  }

  void write_stats()
  {
    if (output_file_.empty())
      return;
    std::ofstream f(output_file_);
    f << "{\"total_published\": " << image_msg_count_ << "}";
    f.close();
    RCLCPP_INFO(
        this->get_logger(), "Stats written to %s", output_file_.c_str());
  }
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
