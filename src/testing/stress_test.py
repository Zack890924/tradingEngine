import socket
import struct
import time
import threading
import argparse
from queue import Queue
import statistics

def send_xml_request(xml_str):
    host = 'localhost'
    port = 12345
    start = time.time()
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5)  # 設定 5 秒超時
            s.connect((host, port))
            xml_bytes = xml_str.encode('utf-8')
            s.sendall(struct.pack("!I", len(xml_bytes)) + xml_bytes)
            raw_len = s.recv(4)
            if len(raw_len) < 4:
                return None, None
            msg_len = struct.unpack("!I", raw_len)[0]
            data = b''
            while len(data) < msg_len:
                chunk = s.recv(msg_len - len(data))
                if not chunk:
                    break
                data += chunk
        end = time.time()
        return data.decode(), end - start
    except Exception as e:
        return None, None

def worker(xml_str, request_queue, results, latencies):
    while not request_queue.empty():
        try:
            request_queue.get_nowait()
        except:
            break
        resp, dur = send_xml_request(xml_str)
        if resp is not None:
            results.append(resp)
            latencies.append(dur)
        request_queue.task_done()

def stress_test(num_requests, xml_str, num_threads):
    request_queue = Queue()
    for _ in range(num_requests):
        request_queue.put(1)

    threads = []
    results = []
    latencies = []
    start_time = time.time()

    for _ in range(num_threads):
        t = threading.Thread(target=worker, args=(xml_str, request_queue, results, latencies))
        t.start()
        threads.append(t)

    for t in threads:
        t.join()

    total_time = time.time() - start_time
    throughput = num_requests / total_time if total_time > 0 else 0
    avg_latency = statistics.mean(latencies) if latencies else 0
    return total_time, throughput, avg_latency, results

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Stress Test for Trading Server")
    parser.add_argument("--num", type=int, default=100, help="Number of total requests")
    parser.add_argument("--xml", type=str, default="order", choices=["order", "query", "cancel"], help="Type of XML request")
    parser.add_argument("--threads", type=int, default=4, help="Number of concurrent threads (≒ # of cores)")
    parser.add_argument("--runs", type=int, default=10, help="Number of runs to average")
    args = parser.parse_args()

    xml_requests = {
        "order": """<?xml version=\"1.0\"?>\n<transactions id=\"456\">\n  <order sym=\"SPY\" amount=\"50\" limit=\"100\"/>\n</transactions>""",
        "query": """<?xml version=\"1.0\"?>\n<transactions id=\"456\">\n  <query id=\"1\"/>\n</transactions>""",
        "cancel": """<?xml version=\"1.0\"?>\n<transactions id=\"456\">\n  <cancel id=\"1\"/>\n</transactions>"""
    }

    xml_str = xml_requests[args.xml]
    num_requests = args.num
    num_threads = args.threads
    runs = args.runs

    all_throughputs = []
    all_latencies = []
    for i in range(runs):
        print(f"--- Run {i+1} ---")
        total_time, throughput, avg_latency, results = stress_test(num_requests, xml_str, num_threads)
        print(f"Run {i+1}: Total Time: {total_time:.3f} sec, Throughput: {throughput:.2f} TPS, Average Latency: {avg_latency:.3f} sec/request")
        all_throughputs.append(throughput)
        all_latencies.append(avg_latency)
        time.sleep(1)

    overall_throughput = sum(all_throughputs) / len(all_throughputs)
    overall_latency = sum(all_latencies) / len(all_latencies)

    print("\n===== Average Stress Test Results =====")
    print("Total Requests: {}".format(num_requests))
    print("Threads (≈ cores): {}".format(num_threads))
    print("Average Throughput (TPS): {:.2f}".format(overall_throughput))
    print("Average Latency: {:.3f} seconds/request".format(overall_latency))
