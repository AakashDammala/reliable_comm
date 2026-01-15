#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "rclcpp/logging.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/serialization.hpp"
#include "reliable_comm/msg/reliable_envelope.hpp"
#include "reliable_comm/msg/reliable_feedback_v2.hpp"

using namespace std::chrono_literals;

// Alias for the envelope and feedback types
using Envelope = reliable_comm::msg::ReliableEnvelope;
using FeedbackV2 = reliable_comm::msg::ReliableFeedbackV2;

/**
 * ReliableSub V2 - NACK-based reliable subscriber
 *
 * Instead of storing all received message IDs, tracks only:
 * - last_received_: highest sequential message received
 * - missing_set_: gaps in the sequence
 *
 * Feedback contains last_received + list of missing IDs for efficient
 * retransmit.
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
        last_received_(0),
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

    // Publisher for feedback (V2 with missing list)
    rf_pub_ = this->create_publisher<FeedbackV2>(feedback_topic_name_, qos);

    // Subscribe to envelope topic
    rp_sub_ = this->create_subscription<Envelope>(
        subscribe_topic_name_,
        qos,
        [this](const Envelope::ConstSharedPtr msg,
               const rclcpp::MessageInfo& info) { this->rp_cb(msg, info); });

    RCLCPP_INFO(this->get_logger(),
                "ReliableSub started: qos_depth=%zu",
                qos_history_depth_);
  }

 private:
  void rp_cb(const Envelope::ConstSharedPtr envelope_ptr,
             const rclcpp::MessageInfo& /* info */)
  {
    uint64_t msg_num = envelope_ptr->msg_num;
    bool is_new_message = false;

    // Process message based on sequence number
    if (last_received_ == 0 && missing_set_.empty())
    {
      // First message ever received
      last_received_ = msg_num;
      is_new_message = true;
    }
    else if (msg_num == last_received_ + 1)
    {
      // Next expected message in sequence
      last_received_ = msg_num;
      is_new_message = true;

      // Check if we can advance past any previously missing messages
      while (missing_set_.count(last_received_ + 1) > 0)
      {
        missing_set_.erase(last_received_ + 1);
        last_received_++;
      }
    }
    else if (msg_num > last_received_ + 1)
    {
      // Gap detected - add missing IDs to set
      for (uint64_t i = last_received_ + 1; i < msg_num; ++i)
      {
        missing_set_.insert(i);
      }
      last_received_ = msg_num;
      is_new_message = true;
    }
    else if (msg_num <= last_received_)
    {
      // Retransmitted message - check if it fills a gap
      if (missing_set_.count(msg_num) > 0)
      {
        missing_set_.erase(msg_num);
        is_new_message = true;
        RCLCPP_INFO(this->get_logger(), "Filled gap for msg ID: %lu", msg_num);
      }
      else
      {
        RCLCPP_DEBUG(this->get_logger(),
                     "Duplicate msg ID: %lu already received",
                     msg_num);
      }
    }

    // Forward new messages
    if (is_new_message)
    {
      // Deserialize the payload to the original message type
      rclcpp::Serialization<T> serializer;

      size_t data_size = envelope_ptr->serialized_data.size();
      rclcpp::SerializedMessage serialized_msg(data_size);

      auto& rcl_msg = serialized_msg.get_rcl_serialized_message();
      std::memcpy(
          rcl_msg.buffer, envelope_ptr->serialized_data.data(), data_size);
      rcl_msg.buffer_length = data_size;

      T deserialized_msg;
      serializer.deserialize_message(&serialized_msg, &deserialized_msg);

      RCLCPP_INFO(this->get_logger(), "Publishing msg ID: %lu", msg_num);
      rs_pub_->publish(deserialized_msg);
    }

    // Send feedback with last_received and missing list
    send_feedback();
  }

  void send_feedback()
  {
    FeedbackV2 fb_msg;
    fb_msg.last_received = last_received_;

    // Convert set to vector for message
    fb_msg.missing_msgs.assign(missing_set_.begin(), missing_set_.end());

    rf_pub_->publish(fb_msg);

    RCLCPP_DEBUG(this->get_logger(),
                 "Sent feedback: last=%lu, missing=%zu",
                 last_received_,
                 missing_set_.size());
  }

  typename rclcpp::Subscription<Envelope>::SharedPtr rp_sub_;
  typename rclcpp::Publisher<T>::SharedPtr rs_pub_;
  typename rclcpp::Publisher<FeedbackV2>::SharedPtr rf_pub_;

  std::set<uint64_t> missing_set_;  // Gaps in sequence
  uint64_t last_received_;          // Highest sequential msg received

  std::string input_topic_name_, subscribe_topic_name_, publish_topic_name_,
      feedback_topic_name_;

  size_t qos_history_depth_;
};
