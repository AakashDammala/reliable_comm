#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <rclcpp/serialized_message.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rclcpp/logging.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/serialization.hpp"
#include "reliable_comm/msg/reliable_envelope.hpp"
#include "rmw/types.h"
#include "std_msgs/msg/int64.hpp"

using namespace std::chrono_literals;

// Use Int64 for feedback to match msg_num type (uint64)
typedef std_msgs::msg::Int64 feedback_msg;

// Alias for the envelope type
using Envelope = reliable_comm::msg::ReliableEnvelope;

template <typename T>
class ReliablePub : public rclcpp::Node
{
 public:
  ReliablePub(std::string& node_name, std::string& input_topic_name)
      : Node(node_name), input_topic_name_(input_topic_name), msg_num_(0)
  {
    // input: '/image'   publish: '/rp_image'   feedback: '/rf_image'
    publish_topic_name_ = "/rp_" + input_topic_name_.substr(1);
    feedback_topic_name_ = "/rf_" + input_topic_name_.substr(1);

    // BEST_EFFORT QoS with large buffer - let our layer handle reliability
    auto qos = rclcpp::QoS(1000).best_effort();

    rp_pub_ = this->create_publisher<Envelope>(publish_topic_name_, qos);

    feedback_sub_ = this->create_subscription<feedback_msg>(
        feedback_topic_name_,
        qos,
        std::bind(&ReliablePub::feedback_cb, this, std::placeholders::_1));

    re_publisher_thread_ =
        std::thread(&ReliablePub::re_publisher_function, this);

    msg_sub_ = this->create_subscription<T>(input_topic_name_,
                                            qos,
                                            std::bind(&ReliablePub::msg_cb,
                                                      this,
                                                      std::placeholders::_1,
                                                      std::placeholders::_2));
  }

  ~ReliablePub()
  {
    if (re_publisher_thread_.joinable())
    {
      re_publisher_thread_.join();
    }
  }

 private:
  void msg_cb(const typename T::ConstSharedPtr msg_ptr,
              const rclcpp::MessageInfo& /* info */)
  {
    // Serialize the message
    rclcpp::Serialization<T> serializer;
    rclcpp::SerializedMessage serialized_msg;
    serializer.serialize_message(msg_ptr.get(), &serialized_msg);

    // Create envelope with msg_num
    auto envelope = std::make_shared<Envelope>();
    envelope->msg_num = msg_num_;

    // Get the serialized buffer and assign to envelope
    auto& rcl_msg = serialized_msg.get_rcl_serialized_message();
    envelope->serialized_data.assign(rcl_msg.buffer,
                                     rcl_msg.buffer + rcl_msg.buffer_length);

    // Store the envelope in msg_storage_
    {
      std::lock_guard<std::mutex> lock(msg_storage_mutex_);
      msg_storage_.emplace(msg_num_, envelope);
    }

    RCLCPP_INFO(this->get_logger(), "Stored msg ID: %lu", msg_num_);

    // Publish the envelope
    publish_msg(envelope);

    msg_num_++;
  }

  void feedback_cb(const feedback_msg::ConstSharedPtr msg_ptr)
  {
    RCLCPP_INFO(this->get_logger(), "Feedback received: %ld", msg_ptr->data);

    std::lock_guard<std::mutex> lock(msg_storage_mutex_);

    uint64_t unique_id = static_cast<uint64_t>(msg_ptr->data);
    auto it = msg_storage_.find(unique_id);

    if (it != msg_storage_.end())
    {
      msg_storage_.erase(it);

      RCLCPP_INFO(this->get_logger(),
                  "Got feedback for %lu, removing msg from storage.",
                  unique_id);
    }
    else
    {
      RCLCPP_WARN(this->get_logger(),
                  "Feedback for unique id: %lu received, but it is not present "
                  "in msg_storage_",
                  unique_id);
    }
  }

  void re_publisher_function()
  {
    rclcpp::Rate loop_rate(1.0);

    while (rclcpp::ok())
    {
      msg_storage_mutex_.lock();
      size_t num_msg_in_storage = msg_storage_.size();
      msg_storage_mutex_.unlock();

      if (num_msg_in_storage > 0)
      {
        std::lock_guard<std::mutex> lock(msg_storage_mutex_);

        size_t count = 0;
        constexpr size_t MAX_RETRIES_PER_LOOP = 25;

        for (auto const& [id, msg_ptr] : msg_storage_)
        {
          if (count >= MAX_RETRIES_PER_LOOP)
            break;

          publish_msg(msg_ptr);
          count++;

          // slight delay, to not just dump all msgs at once
          rclcpp::sleep_for(std::chrono::milliseconds(2));
        }
      }

      loop_rate.sleep();
    }
  }

  void publish_msg(Envelope::ConstSharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(rp_pub_mutex_);

    if (msg)
      rp_pub_->publish(*msg);
    else
      RCLCPP_WARN(this->get_logger(), "msg ptr empty in publish_msg function");
  }

  typename rclcpp::Subscription<T>::SharedPtr msg_sub_;
  typename rclcpp::Publisher<Envelope>::SharedPtr rp_pub_;
  rclcpp::Subscription<feedback_msg>::SharedPtr feedback_sub_;
  std::thread re_publisher_thread_;

  std::unordered_map<uint64_t, typename std::shared_ptr<Envelope>> msg_storage_;

  std::mutex rp_pub_mutex_, msg_storage_mutex_;

  std::string input_topic_name_, publish_topic_name_, feedback_topic_name_;

  uint64_t msg_num_;
};