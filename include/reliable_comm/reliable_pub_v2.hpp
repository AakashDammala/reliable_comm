#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <rclcpp/serialized_message.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
 * ReliablePub V2 - NACK-based reliable publisher
 *
 * Uses V2 feedback to efficiently manage retransmissions:
 * - Clears all messages up to last_received (except missing ones)
 * - Only retries messages in the missing list
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
        msg_num_(0),
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

    rp_pub_ = this->create_publisher<Envelope>(publish_topic_name_, qos);

    feedback_sub_ = this->create_subscription<FeedbackV2>(
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
                "ReliablePub started: qos_depth=%zu, max_retries=%zu, "
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

    // Check storage size before adding
    {
      std::lock_guard<std::mutex> lock(msg_storage_mutex_);
      if (msg_storage_.size() < max_storage_size_)
      {
        msg_storage_.emplace(msg_num_, envelope);
        RCLCPP_INFO(this->get_logger(), "Stored msg ID: %lu", msg_num_);
      }
      else
      {
        RCLCPP_WARN(this->get_logger(),
                    "Storage full (%zu), not storing msg ID: %lu",
                    max_storage_size_,
                    msg_num_);
      }
    }

    // Always publish the envelope (even if not stored)
    publish_msg(envelope);

    msg_num_++;
  }

  void feedback_cb(const FeedbackV2::ConstSharedPtr fb_ptr)
  {
    std::lock_guard<std::mutex> lock(msg_storage_mutex_);

    uint64_t last_received = fb_ptr->last_received;

    // Build set of missing IDs for O(1) lookup
    std::unordered_set<uint64_t> missing_set(fb_ptr->missing_msgs.begin(),
                                             fb_ptr->missing_msgs.end());

    // Remove all acknowledged messages (up to last_received, not in missing)
    size_t removed_count = 0;
    for (auto it = msg_storage_.begin(); it != msg_storage_.end();)
    {
      uint64_t id = it->first;
      if (id <= last_received && missing_set.count(id) == 0)
      {
        it = msg_storage_.erase(it);
        removed_count++;
      }
      else
      {
        ++it;
      }
    }

    if (removed_count > 0)
    {
      RCLCPP_INFO(this->get_logger(),
                  "Feedback: last=%lu, missing=%zu, cleared=%zu, remaining=%zu",
                  last_received,
                  missing_set.size(),
                  removed_count,
                  msg_storage_.size());
    }
  }

  void re_publisher_function()
  {
    rclcpp::Rate loop_rate(retry_rate_hz_);

    while (rclcpp::ok())
    {
      msg_storage_mutex_.lock();
      size_t num_msg_in_storage = msg_storage_.size();
      msg_storage_mutex_.unlock();

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
  rclcpp::Subscription<FeedbackV2>::SharedPtr feedback_sub_;
  std::thread re_publisher_thread_;

  std::unordered_map<uint64_t, typename std::shared_ptr<Envelope>> msg_storage_;

  std::mutex rp_pub_mutex_, msg_storage_mutex_;

  std::string input_topic_name_, publish_topic_name_, feedback_topic_name_;

  uint64_t msg_num_;

  // Configurable parameters
  size_t qos_history_depth_;
  size_t max_retries_per_loop_;
  double retry_rate_hz_;
  size_t max_storage_size_;
};
