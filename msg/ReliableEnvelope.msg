# ReliableEnvelope - Wraps any serialized message with a sequence number
# for reliable communication with acknowledgment/retransmission

uint64 msg_num           # Message sequence number for ACK/retransmit
uint8[] serialized_data  # Serialized payload (any ROS2 message type)
