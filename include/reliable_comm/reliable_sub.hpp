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
#include "rmw/types.h"
#include "std_msgs/msg/int64.hpp"

using namespace std::chrono_literals;

typedef std_msgs::msg::Int64 feedback_msg;

/**
 * ReliableSub V0 - ACK-based reliable subscriber
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

    rs_pub_ = this->create_publisher<T>(publish_topic_name_, qos);

    rf_pub_ = this->create_publisher<feedback_msg>(feedback_topic_name_, qos);

    rp_sub_ = this->create_subscription<T>(
        subscribe_topic_name_,
        qos,
        [this](const typename T::ConstSharedPtr msg,
               const rclcpp::MessageInfo& info) { this->rp_cb(msg, info); });

    RCLCPP_INFO(this->get_logger(),
                "ReliableSub V0 started: qos_depth=%zu",
                qos_history_depth_);
  }

 private:
  void rp_cb(const typename T::ConstSharedPtr msg_ptr,
             const rclcpp::MessageInfo& info)
  {
    uint64_t unique_id =
        info.get_rmw_message_info().publication_sequence_number;

    unique_id = (static_cast<uint64_t>(msg_ptr->header.stamp.sec) << 32) |
                msg_ptr->header.stamp.nanosec;

    RCLCPP_INFO(this->get_logger(), "Got message %lu", unique_id);

    // check if the message has already been received?
    auto it = received_msg_set_.find(unique_id);
    if (it == received_msg_set_.end())  // msg not received earlier
    {
      received_msg_set_.emplace(unique_id);

      RCLCPP_INFO(this->get_logger(), "Publishing msg with id: %lu", unique_id);
      rs_pub_->publish(*msg_ptr);
    }
    else
    {
      RCLCPP_INFO(this->get_logger(),
                  "Msg with id: %lu already passed forward, not passing again.",
                  unique_id);
    }

    // send feedback that the msg is received
    RCLCPP_INFO(
        this->get_logger(), "Publishing feedback with id: %lu", unique_id);
    feedback_msg fb_msg;
    fb_msg.data = unique_id;
    rf_pub_->publish(fb_msg);
  }

  typename rclcpp::Subscription<T>::SharedPtr rp_sub_;
  typename rclcpp::Publisher<T>::SharedPtr rs_pub_;
  typename rclcpp::Publisher<feedback_msg>::SharedPtr rf_pub_;

  std::unordered_set<uint64_t> received_msg_set_;

  std::string input_topic_name_, subscribe_topic_name_, publish_topic_name_,
      feedback_topic_name_;

  size_t qos_history_depth_;
};