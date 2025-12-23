#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "rclcpp/logging.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rmw/types.h"
#include "std_msgs/msg/int64.hpp"

using namespace std::chrono_literals;

typedef std_msgs::msg::Int64 feedback_msg;

// @TODO - check qos settings for publishers & subscribers

template <typename T>
class ReliablePub : public rclcpp::Node
{
 public:
  ReliablePub(std::string& node_name, std::string& input_topic_name)
      : Node(node_name), input_topic_name_(input_topic_name)
  {
    // input: '/image'   publish: '/rp_image'   feedback: '/rf_image'
    publish_topic_name_ = "/rp_" + input_topic_name_.substr(1);
    feedback_topic_name_ = "/rf_" + input_topic_name_.substr(1);

    rp_pub_ = this->create_publisher<T>(publish_topic_name_, 10);

    feedback_sub_ = this->create_subscription<feedback_msg>(
        feedback_topic_name_,
        10,
        std::bind(&ReliablePub::feedback_cb, this, std::placeholders::_1));

    timer_ = this->create_wall_timer(
        5s, std::bind(&ReliablePub::timer_callback, this));

    msg_sub_ = this->create_subscription<T>(input_topic_name_,
                                            10,
                                            std::bind(&ReliablePub::msg_cb,
                                                      this,
                                                      std::placeholders::_1,
                                                      std::placeholders::_2));
  }

 private:
  void msg_cb(const typename T::ConstSharedPtr msg_ptr,
              const rclcpp::MessageInfo& info)
  {
    uint64_t unique_id =
        info.get_rmw_message_info().publication_sequence_number;

    unique_id = (static_cast<uint64_t>(msg_ptr->header.stamp.sec) << 32) |
                msg_ptr->header.stamp.nanosec;

    if (unique_id == 0)
    {
      RCLCPP_WARN(this->get_logger(),
                  "Message has no timestamp or sequence number!");
      return;
    }

    // just store the pointer, don't make a copy
    // Using emplace to avoid overwriting just in case
    msg_storage_mutex_.lock();
    auto inserted = msg_storage_.emplace(unique_id, msg_ptr);
    msg_storage_mutex_.unlock();

    if (inserted.second)
    {
      RCLCPP_INFO(this->get_logger(), "Stored msg ID: %lu", unique_id);

      // publish the message
      publish_msg(msg_ptr);
    }
    else
    {
      RCLCPP_DEBUG(this->get_logger(),
                   "Duplicate ID %lu received, ignoring.",
                   unique_id);
    }
  }

  void feedback_cb(const feedback_msg::ConstSharedPtr msg_ptr)
  {
    std::lock_guard<std::mutex> lock(msg_storage_mutex_);

    uint64_t unique_id = msg_ptr->data;
    auto it = msg_storage_.find(unique_id);

    // check if the msg is in the map.
    // if yes, pop it, else give a warning
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
                  "Feedback for unique id: %ld received, but it is not present "
                  "in msg_storage_",
                  unique_id);
    }
  }

  void timer_callback()
  {
    msg_storage_mutex_.lock();
    size_t num_msg_in_storage = msg_storage_.size();
    msg_storage_mutex_.unlock();

    if (num_msg_in_storage > 0)
    {
      std::lock_guard<std::mutex> lock(msg_storage_mutex_);

      for (auto const& [id, msg_ptr] : msg_storage_)
      {
        publish_msg(msg_ptr);

        // slight delay, to not just dump all msgs at once
        rclcpp::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  void publish_msg(typename T::ConstSharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(rp_pub_mutex_);

    // will this cause any issues? can I publish the same msg repetedly
    if (msg)
      rp_pub_->publish(*msg);
    else
      RCLCPP_WARN(this->get_logger(), "msg ptr empty in publish_msg function");
  }

  typename rclcpp::Subscription<T>::SharedPtr msg_sub_;
  typename rclcpp::Publisher<T>::SharedPtr rp_pub_;
  rclcpp::Subscription<feedback_msg>::SharedPtr feedback_sub_;
  rclcpp::TimerBase::SharedPtr timer_;

  std::unordered_map<uint64_t, typename T::ConstSharedPtr> msg_storage_;

  std::mutex rp_pub_mutex_, msg_storage_mutex_;

  std::string input_topic_name_, publish_topic_name_, feedback_topic_name_;
};