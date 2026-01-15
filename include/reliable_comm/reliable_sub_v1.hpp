#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "rclcpp/logging.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/serialization.hpp"
#include "reliable_comm/msg/reliable_envelope.hpp"
#include "rmw/types.h"
#include "std_msgs/msg/int64.hpp"

using namespace std::chrono_literals;

typedef std_msgs::msg::Int64 feedback_msg;

// Alias for the envelope type
using Envelope = reliable_comm::msg::ReliableEnvelope;

/**
 * ReliableSub V1 - ACK-based reliable subscriber with envelope deserialization
 */
template <typename T>
class ReliableSub : public rclcpp::Node
{
 public:
  ReliableSub(std::string& node_name,
              std::string& input_topic_name,
              size_t qos_history_depth = 1000,
              size_t /* max_retries_per_loop */ = 25,
              double /* retry_rate_hz */ = 1.0,
              size_t /* max_storage_size */ = 1000)
      : Node(node_name),
        input_topic_name_(input_topic_name),
        qos_history_depth_(qos_history_depth)
  {
    // input: '/image'   subscribe: '/rp_image'   publish: '/rs_image'
    // feedback: '/rf_image'
    subscribe_topic_name_ = "/rp_" + input_topic_name_.substr(1);
    publish_topic_name_ = "/rs_" + input_topic_name_.substr(1);
    feedback_topic_name_ = "/rf_" + input_topic_name_.substr(1);

    // BEST_EFFORT QoS with configurable buffer
    auto qos = rclcpp::QoS(qos_history_depth_).best_effort();

    // Publisher for deserialized messages
    rs_pub_ = this->create_publisher<T>(publish_topic_name_, qos);

    // Publisher for feedback (ACK)
    rf_pub_ = this->create_publisher<feedback_msg>(feedback_topic_name_, qos);

    // Subscribe to envelope topic
    rp_sub_ = this->create_subscription<Envelope>(
        subscribe_topic_name_,
        qos,
        [this](const Envelope::ConstSharedPtr msg,
               const rclcpp::MessageInfo& info) { this->rp_cb(msg, info); });

    RCLCPP_INFO(this->get_logger(),
                "ReliableSub V1 started: qos_depth=%zu",
                qos_history_depth_);
  }

 private:
  void rp_cb(const Envelope::ConstSharedPtr envelope_ptr,
             const rclcpp::MessageInfo& /* info */)
  {
    uint64_t msg_num = envelope_ptr->msg_num;

    // Check if the message has already been received
    auto it = received_msg_set_.find(msg_num);
    if (it == received_msg_set_.end())  // msg not received earlier
    {
      received_msg_set_.emplace(msg_num);

      // Deserialize the payload to the original message type
      rclcpp::Serialization<T> serializer;

      // Create SerializedMessage with proper capacity
      size_t data_size = envelope_ptr->serialized_data.size();
      rclcpp::SerializedMessage serialized_msg(data_size);

      // Get reference and copy data
      auto& rcl_msg = serialized_msg.get_rcl_serialized_message();
      std::memcpy(
          rcl_msg.buffer, envelope_ptr->serialized_data.data(), data_size);
      rcl_msg.buffer_length = data_size;

      // Deserialize to message type T
      T deserialized_msg;
      serializer.deserialize_message(&serialized_msg, &deserialized_msg);

      RCLCPP_INFO(this->get_logger(), "Publishing msg with id: %lu", msg_num);
      rs_pub_->publish(deserialized_msg);
    }
    else
    {
      RCLCPP_INFO(this->get_logger(),
                  "Msg with id: %lu already passed forward, not passing again.",
                  msg_num);
    }

    // Send feedback that the msg is received
    RCLCPP_INFO(
        this->get_logger(), "Publishing feedback with id: %lu", msg_num);
    feedback_msg fb_msg;
    fb_msg.data = static_cast<int64_t>(msg_num);
    rf_pub_->publish(fb_msg);
  }

  typename rclcpp::Subscription<Envelope>::SharedPtr rp_sub_;
  typename rclcpp::Publisher<T>::SharedPtr rs_pub_;
  typename rclcpp::Publisher<feedback_msg>::SharedPtr rf_pub_;

  std::unordered_set<uint64_t> received_msg_set_;

  std::string input_topic_name_, subscribe_topic_name_, publish_topic_name_,
      feedback_topic_name_;

  size_t qos_history_depth_;
};
