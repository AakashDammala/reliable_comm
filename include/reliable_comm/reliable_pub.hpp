#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rclcpp/logging.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rmw/types.h"
#include "std_msgs/msg/int64.hpp"

using namespace std::chrono_literals;

typedef std_msgs::msg::Int64 feedback_msg;

/**
 * ReliablePub V0 - ACK-based reliable publisher
 */
template <typename T>
class ReliablePub : public rclcpp::Node
{
 public:
  ReliablePub(std::string& node_name,
              std::string& input_topic_name,
              size_t qos_history_depth = 1000,
              size_t max_retries_per_loop = 25,
              double retry_rate_hz = 1.0,
              size_t max_storage_size = 1000)
      : Node(node_name),
        input_topic_name_(input_topic_name),
        last_feedback_time_(std::chrono::steady_clock::now()),
        publishing_complete_(false),
        qos_history_depth_(qos_history_depth),
        max_retries_per_loop_(max_retries_per_loop),
        retry_rate_hz_(retry_rate_hz),
        max_storage_size_(max_storage_size)
  {
    // input: '/image'   publish: '/rp_image'   feedback: '/rf_image'
    publish_topic_name_ = "/rp_" + input_topic_name_.substr(1);
    feedback_topic_name_ = "/rf_" + input_topic_name_.substr(1);

    // BEST_EFFORT QoS with configurable buffer
    auto qos = rclcpp::QoS(qos_history_depth_).best_effort();

    rp_pub_ = this->create_publisher<T>(publish_topic_name_, qos);

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

    RCLCPP_INFO(this->get_logger(),
                "ReliablePub V0 started: qos_depth=%zu, max_retries=%zu, "
                "retry_rate=%.1fHz, max_storage=%zu",
                qos_history_depth_,
                max_retries_per_loop_,
                retry_rate_hz_,
                max_storage_size_);
  }

  ~ReliablePub()
  {
    if (re_publisher_thread_.joinable())
    {
      re_publisher_thread_.join();
    }
  }

  // Called by external node to signal that all messages have been published
  void set_publishing_complete()
  {
    publishing_complete_ = true;
    last_feedback_time_ = std::chrono::steady_clock::now();
    RCLCPP_INFO(this->get_logger(),
                "Publishing complete, will shutdown after 60s of no feedback");
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
    // Check storage size before adding
    msg_storage_mutex_.lock();
    if (msg_storage_.size() < max_storage_size_)
    {
      msg_storage_.emplace(unique_id, msg_ptr);
      msg_storage_mutex_.unlock();
      RCLCPP_INFO(this->get_logger(), "Stored msg ID: %lu", unique_id);
    }
    else
    {
      msg_storage_mutex_.unlock();
      RCLCPP_WARN(this->get_logger(),
                  "Storage full (%zu), not storing msg ID: %lu",
                  max_storage_size_,
                  unique_id);
    }

    // Always publish the envelope (even if not stored)
    publish_msg(msg_ptr);
  }

  void feedback_cb(const feedback_msg::ConstSharedPtr msg_ptr)
  {
    RCLCPP_INFO(this->get_logger(), "Feedback received: %ld", msg_ptr->data);

    // Update last feedback time
    last_feedback_time_ = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(msg_storage_mutex_);

    uint64_t unique_id = msg_ptr->data;
    auto it = msg_storage_.find(unique_id);

    if (it != msg_storage_.end())
    {
      msg_storage_.erase(it);

      RCLCPP_INFO(this->get_logger(),
                  "Got feedback for %lu, removing msg from storage. %zu "
                  "remaining.",
                  unique_id,
                  msg_storage_.size());
    }
    else
    {
      RCLCPP_WARN(this->get_logger(),
                  "Feedback for unique id: %ld received, but it is not present "
                  "in msg_storage_",
                  unique_id);
    }
  }

  void re_publisher_function()
  {
    rclcpp::Rate loop_rate(retry_rate_hz_);
    constexpr int IDLE_TIMEOUT_SEC = 60;

    while (rclcpp::ok())
    {
      msg_storage_mutex_.lock();
      size_t num_msg_in_storage = msg_storage_.size();
      msg_storage_mutex_.unlock();

      // Check for idle timeout after publishing is complete
      if (publishing_complete_)
      {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           now - last_feedback_time_)
                           .count();

        // If all messages acknowledged or idle for 60 seconds, shutdown
        if (num_msg_in_storage == 0)
        {
          RCLCPP_INFO(this->get_logger(),
                      "All messages acknowledged, shutting down");
          rclcpp::shutdown();
          return;
        }
        else if (elapsed >= IDLE_TIMEOUT_SEC)
        {
          RCLCPP_WARN(this->get_logger(),
                      "No feedback for %ld seconds with %zu unacked messages, "
                      "shutting down",
                      elapsed,
                      num_msg_in_storage);
          rclcpp::shutdown();
          return;
        }
      }

      if (num_msg_in_storage > 0)
      {
        std::lock_guard<std::mutex> lock(msg_storage_mutex_);

        size_t count = 0;

        for (auto const& [id, msg_ptr] : msg_storage_)
        {
          if (count >= max_retries_per_loop_)
            break;

          publish_msg(msg_ptr);
          count++;

          rclcpp::sleep_for(std::chrono::milliseconds(2));
        }
      }

      loop_rate.sleep();
    }
  }

  void publish_msg(typename T::ConstSharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(rp_pub_mutex_);

    if (msg)
      rp_pub_->publish(*msg);
    else
      RCLCPP_WARN(this->get_logger(), "msg ptr empty in publish_msg function");
  }

  typename rclcpp::Subscription<T>::SharedPtr msg_sub_;
  typename rclcpp::Publisher<T>::SharedPtr rp_pub_;
  rclcpp::Subscription<feedback_msg>::SharedPtr feedback_sub_;
  std::thread re_publisher_thread_;

  std::unordered_map<uint64_t, typename T::ConstSharedPtr> msg_storage_;

  std::mutex rp_pub_mutex_, msg_storage_mutex_;

  std::string input_topic_name_, publish_topic_name_, feedback_topic_name_;

  // Idle timeout tracking
  std::chrono::steady_clock::time_point last_feedback_time_;
  std::atomic<bool> publishing_complete_;

  // Configurable parameters
  size_t qos_history_depth_;
  size_t max_retries_per_loop_;
  double retry_rate_hz_;
  size_t max_storage_size_;
};