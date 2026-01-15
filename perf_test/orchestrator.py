#!/usr/bin/env python3
"""
Performance test orchestrator for reliable_comm.
Runs pub/sub in Docker containers with Pumba network fault injection.
"""

import subprocess
import time
import json
import csv
import os
import threading
from datetime import datetime
from pathlib import Path

# Configuration
RESULTS_DIR = Path(__file__).parent / "results"
COMPOSE_FILE = Path(__file__).parent / "docker-compose.yml"

# Default node parameters
DEFAULT_PARAMS = {
    "image_publish_frequency": 30,
    "image_size": "32p",
    "image_num_messages": 500,
    "twist_publish_frequency": 0,
    "twist_num_messages": 0,
}

# Test configurations - each can override default params
# tc_args: arguments for tc qdisc netem (applied to both containers)
TEST_CASES = [
    {
        "name": "baseline",
        "tc_args": None,
        "timeout_buffer_sec": 30,
        "image_size": "32p"
    },
    {
        "name": "loss_15pct",
        "tc_args": "loss 15%",
        "timeout_buffer_sec": 60,
        "image_size": "32p"
    },
    {
        "name": "loss_30pct",
        "tc_args": "loss 30%",
        "timeout_buffer_sec": 420,
        "image_size": "32p"
    },
    {
        "name": "blackout_10s_every_40s",
        "tc_args": "periodic_blackout",
        "blackout_on_sec": 10,      # 10 seconds of 100% packet loss
        "blackout_off_sec": 40,     # 40 seconds normal
        "timeout_buffer_sec": 420,
        "image_size": "32p"
    },
    {
        "name": "delay_100ms",
        "tc_args": "delay 100ms",
        "timeout_buffer_sec": 420,
        "image_size": "32p"
    },
    {
        "name": "baseline",
        "tc_args": None,
        "timeout_buffer_sec": 30,
        "image_size": "240p"
    },
    {
        "name": "loss_15pct",
        "tc_args": "loss 15%",
        "timeout_buffer_sec": 60,
        "image_size": "240p"
    },
    {
        "name": "loss_30pct",
        "tc_args": "loss 30%",
        "timeout_buffer_sec": 420,
        "image_size": "240p"
    },
    {
        "name": "blackout_10s_every_40s",
        "tc_args": "periodic_blackout",
        "blackout_on_sec": 10,      # 10 seconds of 100% packet loss
        "blackout_off_sec": 40,     # 40 seconds normal
        "timeout_buffer_sec": 420,
        "image_size": "240p"
    },
    {
        "name": "delay_100ms",
        "tc_args": "delay 100ms",
        "timeout_buffer_sec": 420,
        "image_size": "240p"
    },
    {
        "name": "baseline",
        "tc_args": None,
        "timeout_buffer_sec": 30,
        "image_size": "720p"
    },
    {
        "name": "loss_15pct",
        "tc_args": "loss 15%",
        "timeout_buffer_sec": 60,
        "image_size": "720p"
    },
    {
        "name": "loss_30pct",
        "tc_args": "loss 30%",
        "timeout_buffer_sec": 420,
        "image_size": "720p"
    },
    {
        "name": "blackout_10s_every_40s",
        "tc_args": "periodic_blackout",
        "blackout_on_sec": 10,      # 10 seconds of 100% packet loss
        "blackout_off_sec": 40,     # 40 seconds normal
        "timeout_buffer_sec": 420,
        "image_size": "720p"
    },
    {
        "name": "delay_100ms",
        "tc_args": "delay 100ms",
        "timeout_buffer_sec": 420,
        "image_size": "720p"
    },
]

# Default timeout buffer in seconds (added to calculated publish time)
# Can be overridden per test case with "timeout_buffer_sec" key
DEFAULT_TIMEOUT_BUFFER_SEC = 60


def get_params(test_case):
    """Merge test case params with defaults."""
    params = DEFAULT_PARAMS.copy()
    for key in DEFAULT_PARAMS:
        if key in test_case:
            params[key] = test_case[key]
    return params


def run_cmd(cmd, check=True):
    """Run shell command."""
    print(f"[CMD] {' '.join(cmd)}")
    return subprocess.run(cmd, check=check, capture_output=True, text=True)


def calculate_timeout(params, timeout_buffer_sec):
    """Calculate timeout = (num_messages / frequency) + buffer."""
    freq = params.get("image_publish_frequency", 30)
    msgs = params.get("image_num_messages", 1000)
    publish_time = msgs / freq if freq > 0 else 60
    return int(publish_time + timeout_buffer_sec)


def start_containers(params, timeout_sec):
    """Start pub and sub containers with given parameters."""
    # Build ROS args for publisher
    pub_ros_args = [f"-p {k}:={v}" for k, v in params.items()]
    pub_ros_args.append(f"-p output_file:=/results/pub_stats.json")
    pub_ros_args.append(f"-p timeout_sec:={timeout_sec}")
    pub_args_str = "--ros-args " + " ".join(pub_ros_args)
    
    # Build ROS args for subscriber
    sub_ros_args = [
        f"-p expected_messages:={params['image_num_messages']}",
        f"-p output_file:=/results/sub_stats.json",
        f"-p timeout_sec:={timeout_sec}",
    ]
    sub_args_str = "--ros-args " + " ".join(sub_ros_args)
    
    # Clean up any existing containers first
    run_cmd(["docker", "rm", "-f", "perf_pub", "perf_sub"], check=False)
    
    # Build images fresh (remove old images first to force rebuild)
    run_cmd(["docker", "compose", "-f", str(COMPOSE_FILE), "down", "--rmi", "local"], check=False)
    run_cmd(["docker", "compose", "-f", str(COMPOSE_FILE), "build", "--no-cache"])
    
    # Restart with params using docker run directly
    pub_cmd = f"ros2 run reliable_comm regular_pub {pub_args_str}"
    sub_cmd = f"ros2 run reliable_comm regular_sub {sub_args_str}"
    
    print(f"[TIMEOUT] {timeout_sec}s (publish: {params['image_num_messages']}/{params['image_publish_frequency']}Hz, size: {params['image_size']})")
    
    # Create network if it doesn't exist
    run_cmd(["docker", "network", "create", "perf_net"], check=False)
    
    # Use docker run directly with the built images
    subprocess.Popen([
        "docker", "run", "-d", "--rm",
        "--name", "perf_pub",
        "--network", "perf_net",
        "--cap-add", "NET_ADMIN",
        "-v", f"{RESULTS_DIR}:/results",
        "perf_test-pub",
        "bash", "-c", pub_cmd
    ])
    subprocess.Popen([
        "docker", "run", "-d", "--rm",
        "--name", "perf_sub",
        "--network", "perf_net",
        "--cap-add", "NET_ADMIN",
        "-v", f"{RESULTS_DIR}:/results",
        "perf_test-sub",
        "bash", "-c", sub_cmd
    ])
    time.sleep(3)


def stop_containers():
    """Stop all containers, remove images, and clean up network."""
    run_cmd(["docker", "compose", "-f", str(COMPOSE_FILE), "down", "--remove-orphans", "--rmi", "all"], check=False)
    run_cmd(["docker", "rm", "-f", "perf_pub", "perf_sub"], check=False)
    run_cmd(["docker", "network", "rm", "perf_test_perf_net"], check=False)


def apply_tc(tc_args, containers):
    """Apply tc netem rules to containers via docker exec."""
    if not tc_args:
        return
    for container in containers:
        cmd = ["docker", "exec", container, "tc", "qdisc", "add", "dev", "eth0", "root", "netem"] + tc_args.split()
        print(f"[TC] {' '.join(cmd)}")
        try:
            subprocess.run(cmd, check=True, capture_output=True)
        except subprocess.CalledProcessError as e:
            print(f"[WARN] Failed to apply tc to {container}: {e.stderr.decode() if e.stderr else e}")


def remove_tc(containers):
    """Remove tc netem rules from containers."""
    for container in containers:
        cmd = ["docker", "exec", container, "tc", "qdisc", "del", "dev", "eth0", "root"]
        subprocess.run(cmd, check=False, capture_output=True)


def apply_periodic_blackout(containers, on_sec, off_sec, stop_event):
    """Apply periodic blackout: 100% loss for on_sec, then normal for off_sec."""
    cycle_num = 0
    while not stop_event.is_set():
        # Blackout ON - 100% packet loss
        cycle_num += 1
        print(f"[BLACKOUT] Cycle {cycle_num}: ON (100% loss for {on_sec}s)")
        for container in containers:
            cmd = ["docker", "exec", container, "tc", "qdisc", "replace",
                   "dev", "eth0", "root", "netem", "loss", "100%"]
            subprocess.run(cmd, capture_output=True)
        
        # Wait for blackout duration (check stop_event periodically)
        for _ in range(on_sec * 10):
            if stop_event.is_set():
                return
            time.sleep(0.1)
        
        # Blackout OFF - remove loss
        print(f"[BLACKOUT] Cycle {cycle_num}: OFF (normal for {off_sec}s)")
        for container in containers:
            cmd = ["docker", "exec", container, "tc", "qdisc", "replace",
                   "dev", "eth0", "root", "netem", "delay", "0ms"]
            subprocess.run(cmd, capture_output=True)
        
        # Wait for normal duration
        for _ in range(off_sec * 10):
            if stop_event.is_set():
                return
            time.sleep(0.1)


def collect_docker_stats(stop_event, stats_list):
    """Collect docker stats in background thread."""
    while not stop_event.is_set():
        try:
            result = subprocess.run(
                ["docker", "stats", "--no-stream", "--format", 
                 "{{.Name}},{{.CPUPerc}},{{.MemUsage}}"],
                capture_output=True, text=True, timeout=5
            )
            for line in result.stdout.strip().split('\n'):
                if line and ('perf_pub' in line or 'perf_sub' in line):
                    parts = line.split(',')
                    if len(parts) >= 3:
                        stats_list.append({
                            "time": time.time(),
                            "container": parts[0],
                            "cpu": parts[1].replace('%', ''),
                            "mem": parts[2]
                        })
        except Exception as e:
            pass
        time.sleep(1)


def stream_logs(container_name, stop_event):
    """Stream logs from a container in real-time."""
    proc = subprocess.Popen(
        ["docker", "logs", "-f", container_name],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    while not stop_event.is_set():
        line = proc.stdout.readline()
        if line:
            print(f"    [{container_name}] {line.strip()}")
        elif proc.poll() is not None:
            break
    proc.terminate()


def wait_for_completion(timeout=120):
    """Wait for pub container to exit or timeout."""
    start = time.time()
    while time.time() - start < timeout:
        result = subprocess.run(
            ["docker", "ps", "-q", "-f", "name=perf_pub"],
            capture_output=True, text=True
        )
        if not result.stdout.strip():
            return True
        time.sleep(2)
    return False


def parse_mem(mem_str):
    """Parse memory string like '50.5MiB / 1GiB' to MB."""
    try:
        used = mem_str.split('/')[0].strip()
        if 'GiB' in used:
            return float(used.replace('GiB', '')) * 1024
        elif 'MiB' in used:
            return float(used.replace('MiB', ''))
        elif 'KiB' in used:
            return float(used.replace('KiB', '')) / 1024
    except:
        pass
    return 0


def run_test(test_case):
    """Run a single test case."""
    print(f"\n{'='*50}")
    print(f"Running test: {test_case['name']}")
    print(f"{'='*50}\n")
    
    # Get params for this test (defaults merged with test-specific overrides)
    params = get_params(test_case)
    
    # Clean results
    for f in RESULTS_DIR.glob("*.json"):
        f.unlink()
    
    # Start stats collection
    stats_list = []
    stop_event = threading.Event()
    stats_thread = threading.Thread(target=collect_docker_stats, args=(stop_event, stats_list))
    
    # Calculate timeout (use test-specific buffer or default)
    timeout_buffer = test_case.get("timeout_buffer_sec", DEFAULT_TIMEOUT_BUFFER_SEC)
    timeout_sec = calculate_timeout(params, timeout_buffer)
    
    # Initialize log streaming vars
    log_stop_event = threading.Event()
    pub_log_thread = None
    sub_log_thread = None
    
    try:
        # Start containers
        start_containers(params, timeout_sec)
        stats_thread.start()
        
        # Start log streaming
        pub_log_thread = threading.Thread(target=stream_logs, args=("perf_pub", log_stop_event))
        sub_log_thread = threading.Thread(target=stream_logs, args=("perf_sub", log_stop_event))
        pub_log_thread.start()
        sub_log_thread.start()
        
        # Apply tc rules for network faults
        time.sleep(2)  # Wait for containers to be ready
        
        # Check if this is a periodic blackout test
        blackout_thread = None
        blackout_stop_event = threading.Event()
        tc_args = test_case.get("tc_args")
        
        if tc_args == "periodic_blackout":
            on_sec = test_case.get("blackout_on_sec", 10)
            off_sec = test_case.get("blackout_off_sec", 40)
            print(f"[TC] Starting periodic blackout: {on_sec}s on / {off_sec}s off")
            blackout_thread = threading.Thread(
                target=apply_periodic_blackout,
                args=(["perf_pub", "perf_sub"], on_sec, off_sec, blackout_stop_event)
            )
            blackout_thread.start()
        else:
            apply_tc(tc_args, ["perf_pub", "perf_sub"])
        
        # Wait for completion (use same timeout + small buffer)
        completed = wait_for_completion(timeout=timeout_sec + 30)
        
        # Stop blackout thread if running
        if blackout_thread:
            blackout_stop_event.set()
            blackout_thread.join(timeout=2)
        
        # Remove tc rules
        remove_tc(["perf_pub", "perf_sub"])
        
    finally:
        # Stop log streaming
        log_stop_event.set()
        if pub_log_thread:
            pub_log_thread.join(timeout=2)
        if sub_log_thread:
            sub_log_thread.join(timeout=2)
        
        stop_event.set()
        stats_thread.join(timeout=5)
        stop_containers()
    
    # Aggregate results
    result = {
        "test_name": test_case["name"],
        "timestamp": datetime.now().isoformat(),
        "params": params,
        "pumba_args": test_case.get("pumba_args"),
        "completed": completed,
    }
    
    # Read node outputs
    pub_stats_file = RESULTS_DIR / "pub_stats.json"
    sub_stats_file = RESULTS_DIR / "sub_stats.json"
    
    if pub_stats_file.exists():
        with open(pub_stats_file) as f:
            result["pub_stats"] = json.load(f)
    
    if sub_stats_file.exists():
        with open(sub_stats_file) as f:
            result["sub_stats"] = json.load(f)
    
    # Compute container stats
    for container in ["perf_pub", "perf_sub"]:
        c_stats = [s for s in stats_list if s["container"] == container]
        if c_stats:
            cpus = [float(s["cpu"]) for s in c_stats if s["cpu"]]
            mems = [parse_mem(s["mem"]) for s in c_stats]
            result[f"{container}_cpu_avg"] = sum(cpus) / len(cpus) if cpus else 0
            result[f"{container}_cpu_max"] = max(cpus) if cpus else 0
            result[f"{container}_mem_avg_mb"] = sum(mems) / len(mems) if mems else 0
            result[f"{container}_mem_max_mb"] = max(mems) if mems else 0
    
    return result


def main():
    """Run all test cases."""
    RESULTS_DIR.mkdir(exist_ok=True)
    
    all_results = []
    for test in TEST_CASES:
        result = run_test(test)
        all_results.append(result)
        
        # Save intermediate results
        with open(RESULTS_DIR / "all_results.json", "w") as f:
            json.dump(all_results, f, indent=2)
    
    print("\n" + "="*50)
    print("All tests completed!")
    print(f"Results saved to: {RESULTS_DIR / 'all_results.json'}")


if __name__ == "__main__":
    main()
