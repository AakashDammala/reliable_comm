# Reliable Communication Wrapper for ROS 2

This package provides a template-based wrapper to ensure reliable message delivery between nodes operating in lossy network environments. It uses a "Reliable Publisher" and "Reliable Subscriber" pair to intercept and acknowledge traffic.

## Overview

The system bridges a standard publisher and subscriber by wrapping them with reliable intermediaries within the same executable:

1. **Regular Pub → Reliable Pub**: The regular_pub sends messages on a local topic (e.g., `/image`).
2. **Reliable Pub → Reliable Sub**: The reliable_pub forwards the message over the network on a prefixed topic (e.g., `/rp_image`). It stores the message in a buffer until an acknowledgement is received.
3. **Reliable Sub → Reliable Pub (Feedback)**: Upon receiving a message, the reliable_sub sends a feedback signal (e.g., `/rf_image`) containing the unique ID of the message.
4. **Reliable Sub → Regular Sub**: Once a new message is verified as unique, it is forwarded to the final destination (e.g., `/rs_image`).

<img src="docs/reliable-nodes.png" width="600">

## Key Features

1. **Message Storage**: Messages are stored in a buffer until feedback is received that the messages are received by the other side.
2. **Generic Templates**: Both ReliablePub and ReliableSub are implemented as C++ template classes, allowing them to wrap any ROS 2 message type (e.g., `sensor_msgs/msg/Image`, `std_msgs/msg/String`).
3. **Retrying**: Unacknowledged messages are retried every 2 seconds (0.5 Hz loop), with max 25 messages per retry cycle.
4. **Deduplication**: The ReliableSub uses an unordered_set of 64-bit unique IDs (generated from message timestamps) to ensure that re-transmitted messages are not processed multiple times.
5. **Feedback Loop**: Implements a custom acknowledgement protocol via the `/rf_` topic prefix to signal successful receipt across lossy links.
6. **BEST_EFFORT QoS**: Uses `rclcpp::QoS(1000).best_effort()` to let the custom reliability layer handle retries instead of DDS.
7. **Idle Timeout Shutdown**: Publisher shuts down when all messages are acknowledged OR no feedback received for 60 seconds after publishing completes.

## Available Implementations

| Header | Description |
|--------|-------------|
| `reliable_pub.hpp` / `reliable_sub.hpp` | Basic implementation using timestamps for message IDs |
| `reliable_pub_envelope.hpp` / `reliable_sub_envelope.hpp` | Uses envelope messages with serialized payloads |
| `reliable_pub_v2.hpp` / `reliable_sub_v2.hpp` | NACK-based with missing message tracking |

## Usage Example

To run the ReliablePub or ReliableSub within your existing executable using a MultiThreadedExecutor:

```cpp
#include "reliable_comm/reliable_pub.hpp"
#include "reliable_comm/reliable_sub.hpp"
#include "sensor_msgs/msg/image.hpp"

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);

  std::string node_name = "reliable_pub";
  std::string input_topic_name = "/image";

  // Create ReliablePub first
  auto reliable_pub_node =
      std::make_shared<ReliablePub<sensor_msgs::msg::Image>>(node_name,
                                                             input_topic_name);

  // Create your publisher node with callback to signal when done
  auto main_node = std::make_shared<MyPublisherNode>(
      [reliable_pub_node]() { reliable_pub_node->set_publishing_complete(); });

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(main_node);
  executor.add_node(reliable_pub_node);
  executor.spin();

  rclcpp::shutdown();
  return 0;
}
```

## Performance Testing

See [perf_test/README.md](perf_test/README.md) for Docker-based testing with network fault injection.