# Performance Testing

Docker-based performance testing for ROS2 pub/sub with network fault injection using `tc` (traffic control).

## Prerequisites

- Docker & Docker Compose
- `iproute2` (installed in containers for `tc` command)

## Quick Start

```bash
cd perf_test

# Run all tests
python3 orchestrator.py

# Results saved to perf_test/results/
```

## Test Configuration

Edit `orchestrator.py` to modify:

```python
# Default parameters for all tests
DEFAULT_PARAMS = {
    "image_publish_frequency": 30,
    "image_size": "",              # Options: 1080p, 720p, 480p, 240p, 120p, 96p, 64p, or empty for 32x32
    "image_num_messages": 100,
    "twist_publish_frequency": 0,
    "twist_num_messages": 0,
}

# Test cases with tc netem arguments
TEST_CASES = [
    {"name": "baseline", "tc_args": None},
    {"name": "loss_15pct", "tc_args": "loss 15%"},
    {"name": "loss_30pct", "tc_args": "loss 30%"},
    {"name": "loss_50pct", "tc_args": "loss 50%"},
    {"name": "delay_100ms", "tc_args": "delay 100ms"},
]

# Timeout buffer added to calculated publish time
TIMEOUT_BUFFER_SEC = 60
```

## Network Fault Injection

Uses `tc qdisc netem` for network fault injection:

```python
# Packet loss examples
{"name": "loss_15pct", "tc_args": "loss 15%"}
{"name": "loss_50pct", "tc_args": "loss 50%"}

# Delay examples
{"name": "delay_100ms", "tc_args": "delay 100ms"}
{"name": "delay_200ms_jitter", "tc_args": "delay 200ms 50ms"}

# Combined
{"name": "loss_delay", "tc_args": "loss 10% delay 50ms"}
```

## How It Works

1. **Containers start**: `perf_pub` and `perf_sub` containers launched
2. **TC rules applied**: After 3 seconds, `tc qdisc add dev eth0 root netem` applied to both containers
3. **Publishing**: Publisher waits 4 seconds (startup delay), then publishes messages
4. **Idle timeout**: Publisher stays alive until all messages acknowledged OR no feedback for 60 seconds
5. **Results collected**: Stats from both containers saved to `results/`

## Output

Results JSON (`results/all_results.json`) includes:
- `test_name`, `timestamp`, `params`
- `completed`: Whether test finished normally
- `pub_stats.total_published`: Messages published
- `sub_stats.expected`, `sub_stats.received`, `sub_stats.loss_rate`
- `perf_pub_cpu_avg`, `perf_pub_cpu_max`, `perf_pub_mem_avg_mb`, `perf_pub_mem_max_mb`
- `perf_sub_cpu_avg`, `perf_sub_cpu_max`, `perf_sub_mem_avg_mb`, `perf_sub_mem_max_mb`

## Container Resources

Configured in `docker-compose.yml`:
- **Publisher**: 1 CPU core, 5GB RAM
- **Subscriber**: 1 CPU core, 1GB RAM

## Reliability Layer

The test uses `reliable_pub.hpp` and `reliable_sub.hpp` which provide:
- **BEST_EFFORT QoS**: Lets the custom layer handle retries instead of DDS
- **0.5 Hz retry loop**: Retries up to 25 unacked messages every 2 seconds
- **Idle timeout**: Shuts down after 60 seconds of no feedback
