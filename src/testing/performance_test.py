import subprocess
import time
import matplotlib.pyplot as plt
import numpy as np
import os
from concurrent.futures import ThreadPoolExecutor

# Test configuration
REQUEST_TYPES = ["order", "query", "cancel"]
NUM_REQUESTS = 5000  # Total requests to send for each test
THREAD_COUNTS = [1, 2, 4, 8, 16]  # Thread counts to test
REPEATS = 3  # Number of repeats for each configuration to get stable results

# Create results directory
RESULTS_DIR = "performance_results"
os.makedirs(RESULTS_DIR, exist_ok=True)

def run_test(num_threads, num_requests, request_type, repeat_idx):
    """Run a single test and extract performance metrics"""
    command = f"python test_client.py --num {num_requests} --xml {request_type} --threads {num_threads}"
    
    print(f"Running test: {request_type} with {num_threads} threads, {num_requests} requests (repeat {repeat_idx+1}/{REPEATS})")
    
    try:
        process = subprocess.run(command, shell=True, capture_output=True, text=True)
        output = process.stdout
        
        # Extract metrics from output
        throughput = float(output.split("Throughput (TPS):")[1].split("\n")[0].strip())
        avg_latency = float(output.split("Average Latency:")[1].split("seconds/request")[0].strip())
        
        return throughput, avg_latency
    except Exception as e:
        print(f"Error running test: {e}")
        return 0, 0

def test_request_type(request_type):
    """Run tests for all thread counts for a specific request type"""
    throughputs = []
    latencies = []
    
    for thread_count in THREAD_COUNTS:
        thread_throughputs = []
        thread_latencies = []
        
        for repeat in range(REPEATS):
            tput, lat = run_test(thread_count, NUM_REQUESTS, request_type, repeat)
            thread_throughputs.append(tput)
            thread_latencies.append(lat)
            time.sleep(1)  # Brief pause between tests
        
        # Calculate averages
        avg_throughput = sum(thread_throughputs) / len(thread_throughputs)
        avg_latency = sum(thread_latencies) / len(thread_latencies)
        
        throughputs.append(avg_throughput)
        latencies.append(avg_latency)
        
        print(f"{request_type.capitalize()}: Thread count: {thread_count}, " 
              f"Avg Throughput: {avg_throughput:.2f} TPS, Avg Latency: {avg_latency:.5f}s")
    
    return throughputs, latencies

def create_graphs(all_results):
    """Generate performance graphs"""
    # Combined throughput graph
    plt.figure(figsize=(10, 6))
    for req_type, data in all_results.items():
        plt.plot(THREAD_COUNTS, data["throughputs"], marker='o', linestyle='-', label=req_type.capitalize())
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Throughput (TPS)', fontsize=12)
    plt.title('Throughput vs Number of Threads', fontsize=14)
    plt.grid(True)
    plt.legend()
    plt.savefig(f"{RESULTS_DIR}/combined_throughput.png")
    
    # Combined latency graph
    plt.figure(figsize=(10, 6))
    for req_type, data in all_results.items():
        plt.plot(THREAD_COUNTS, data["latencies"], marker='o', linestyle='-', label=req_type.capitalize())
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Average Latency (seconds)', fontsize=12)
    plt.title('Latency vs Number of Threads', fontsize=14)
    plt.grid(True)
    plt.legend()
    plt.savefig(f"{RESULTS_DIR}/combined_latency.png")
    
    # Scaling efficiency graph (throughput/threads)
    plt.figure(figsize=(10, 6))
    for req_type, data in all_results.items():
        efficiency = [t/tc for t, tc in zip(data["throughputs"], THREAD_COUNTS)]
        plt.plot(THREAD_COUNTS, efficiency, marker='o', linestyle='-', label=req_type.capitalize())
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Efficiency (TPS/Thread)', fontsize=12)
    plt.title('Scaling Efficiency vs Number of Threads', fontsize=14)
    plt.grid(True)
    plt.legend()
    plt.savefig(f"{RESULTS_DIR}/scaling_efficiency.png")

def main():
    all_results = {}
    
    # First, ensure we have some data in the system (create accounts, etc)
    print("Setting up initial test data...")
    subprocess.run("python test.py", shell=True)
    
    # Run tests for each request type
    for request_type in REQUEST_TYPES:
        print(f"\n==== Testing {request_type.upper()} requests ====")
        throughputs, latencies = test_request_type(request_type)
        all_results[request_type] = {
            "throughputs": throughputs,
            "latencies": latencies
        }
    
    # Create performance graphs
    create_graphs(all_results)
    
    # Save results to CSV file
    with open(f"{RESULTS_DIR}/performance_data.csv", "w") as f:
        f.write("RequestType,Threads,Throughput,Latency\n")
        for req_type, data in all_results.items():
            for i, tc in enumerate(THREAD_COUNTS):
                f.write(f"{req_type},{tc},{data['throughputs'][i]:.2f},{data['latencies'][i]:.5f}\n")
    
    print(f"\nPerformance testing complete! Results saved to the '{RESULTS_DIR}' directory.")

if __name__ == "__main__":
    main()