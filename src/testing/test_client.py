import socket
import struct
import time
import threading
import argparse
from queue import Queue

def send_xml_request(xml_str):
    host = 'localhost'
    port = 12345
    start = time.time()
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
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
    avg_latency = sum(latencies) / len(latencies) if latencies else 0
    return total_time, throughput, avg_latency, results

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Stress Test for Trading Server")
    parser.add_argument("--num", type=int, default=100, help="Number of total requests")
    parser.add_argument("--xml", type=str, default="order", choices=["order", "query", "cancel"], help="Type of XML request")
    parser.add_argument("--threads", type=int, default=4, help="Number of concurrent threads (≒ # of cores)")
    args = parser.parse_args()

    xml_requests = {
        "order": """<?xml version=\"1.0\"?>\n<transactions id=\"456\">\n  <order sym=\"SPY\" amount=\"50\" limit=\"100\"/>\n</transactions>""",
        "query": """<?xml version=\"1.0\"?>\n<transactions id=\"456\">\n  <query id=\"1\"/>\n</transactions>""",
        "cancel": """<?xml version=\"1.0\"?>\n<transactions id=\"456\">\n  <cancel id=\"1\"/>\n</transactions>"""
    }

    xml_str = xml_requests[args.xml]
    num_requests = args.num
    num_threads = args.threads

    total_time, throughput, avg_latency, results = stress_test(num_requests, xml_str, num_threads)

    print("\n===== Stress Test Results =====")
    print("Total Requests: {}".format(num_requests))
    print("Threads (≈ cores): {}".format(num_threads))
    print("Total Time: {:.3f} seconds".format(total_time))
    print("Throughput (TPS): {:.2f}".format(throughput))
    print("Average Latency: {:.3f} seconds/request".format(avg_latency))
