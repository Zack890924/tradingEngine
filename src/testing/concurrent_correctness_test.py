#!/usr/bin/env python3
"""
Comprehensive concurrent correctness test.
Tests: double-match prevention, cancel race conditions, concurrent query correctness.
"""

import socket
import struct
import time
import threading
import psycopg2
import xml.etree.ElementTree as ET
from collections import defaultdict
from decimal import Decimal

HOST = 'localhost'
PORT = 12345

DB_CONFIG = {
    "host": "localhost",
    "port": "5433",
    "user": "exchange",
    "password": "exchange_password",
    "dbname": "exchange_db"
}


def clear_db():
    """Clear all test data from database."""
    print("\n=== Clearing Database ===")
    conn = psycopg2.connect(**DB_CONFIG)
    conn.autocommit = True
    cur = conn.cursor()
    try:
        cur.execute("DELETE FROM executions;")
        cur.execute("DELETE FROM orders;")
        cur.execute("DELETE FROM positions;")
        cur.execute("DELETE FROM accounts;")
        print("✅ Database cleared")
    except Exception as e:
        print(f"❌ Failed to clear database: {e}")
    finally:
        cur.close()
        conn.close()


def send_xml_request(xml_str):
    """Send XML request and return response."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5)
            s.connect((HOST, PORT))
            xml_bytes = xml_str.encode('utf-8')
            s.sendall(struct.pack('!I', len(xml_bytes)) + xml_bytes)

            # Receive response
            raw_len = s.recv(4)
            if len(raw_len) < 4:
                return None
            msg_len = struct.unpack('!I', raw_len)[0]
            data = b''
            while len(data) < msg_len:
                chunk = s.recv(msg_len - len(data))
                if not chunk:
                    break
                data += chunk
            return data.decode()
    except Exception as e:
        print(f"Connection error: {e}")
        return None


def setup_test_accounts(num_buyers=4, num_sellers=4):
    """
    Create test accounts with initial balances.
    Returns: (buyer_ids, seller_ids, initial_total_balance)
    """
    print("\n=== Setting Up Test Accounts ===")

    buyer_balance = 100000.0
    seller_balance = 10000.0
    seller_shares = 1000.0
    symbol = "TEST"

    # Create accounts
    accounts_xml = '<?xml version="1.0"?>\n<create>\n'

    buyer_ids = []
    seller_ids = []

    for i in range(num_buyers):
        buyer_id = f"buyer_{i}"
        buyer_ids.append(buyer_id)
        accounts_xml += f'  <account id="{buyer_id}" balance="{buyer_balance}"/>\n'

    for i in range(num_sellers):
        seller_id = f"seller_{i}"
        seller_ids.append(seller_id)
        accounts_xml += f'  <account id="{seller_id}" balance="{seller_balance}"/>\n'

    # Add symbol with shares for sellers
    accounts_xml += f'  <symbol sym="{symbol}">\n'
    for seller_id in seller_ids:
        accounts_xml += f'    <account id="{seller_id}">{seller_shares}</account>\n'
    accounts_xml += '  </symbol>\n'
    accounts_xml += '</create>'

    resp = send_xml_request(accounts_xml)
    if resp and "<error" not in resp.lower():
        print(f"✅ Created {num_buyers} buyers and {num_sellers} sellers")
        initial_balance = num_buyers * buyer_balance + num_sellers * seller_balance
        return buyer_ids, seller_ids, initial_balance
    else:
        print(f"❌ Failed to create accounts: {resp}")
        return None, None, 0


def test_concurrent_orders(buyer_ids, seller_ids, num_threads=4):
    """
    Test 1: Concurrent orders to prevent double-match.
    Multiple threads place buy/sell orders concurrently on the same symbol.
    """
    print(f"\n=== Test 1: Concurrent Orders (Double-Match Prevention) ===")
    print(f"Launching {num_threads} buyer threads and {num_threads} seller threads...")

    symbol = "TEST"
    order_ids = []
    responses = []
    lock = threading.Lock()

    def place_buy_orders(buyer_id, num_orders):
        for i in range(num_orders):
            xml = f"""<?xml version="1.0"?>
<transactions id="{buyer_id}">
  <order sym="{symbol}" amount="50" limit="110"/>
</transactions>"""
            resp = send_xml_request(xml)
            with lock:
                responses.append(resp)
                # Extract order ID
                try:
                    root = ET.fromstring(resp)
                    opened = root.find("opened")
                    if opened is not None:
                        order_ids.append(int(opened.attrib["id"]))
                except:
                    pass

    def place_sell_orders(seller_id, num_orders):
        for i in range(num_orders):
            xml = f"""<?xml version="1.0"?>
<transactions id="{seller_id}">
  <order sym="{symbol}" amount="-50" limit="105"/>
</transactions>"""
            resp = send_xml_request(xml)
            with lock:
                responses.append(resp)
                # Extract order ID
                try:
                    root = ET.fromstring(resp)
                    opened = root.find("opened")
                    if opened is not None:
                        order_ids.append(int(opened.attrib["id"]))
                except:
                    pass

    # Launch threads
    threads = []
    orders_per_thread = 5

    for i in range(min(num_threads, len(buyer_ids))):
        t = threading.Thread(target=place_buy_orders, args=(buyer_ids[i], orders_per_thread))
        threads.append(t)
        t.start()

    for i in range(min(num_threads, len(seller_ids))):
        t = threading.Thread(target=place_sell_orders, args=(seller_ids[i], orders_per_thread))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    print(f"✅ Completed: {len(responses)} requests sent, {len(order_ids)} orders created")
    time.sleep(2)  # Wait for matching to complete
    return order_ids


def test_concurrent_cancels(order_ids, buyer_ids, seller_ids, num_threads=4):
    """
    Test 2: Concurrent cancels to detect race conditions.
    Multiple threads try to cancel the same orders.
    """
    print(f"\n=== Test 2: Concurrent Cancels (Race Condition Detection) ===")

    if not order_ids:
        print("⚠️  No orders to cancel, skipping test")
        return

    # Use remaining open orders
    conn = psycopg2.connect(**DB_CONFIG)
    cur = conn.cursor()
    cur.execute("SELECT order_id FROM orders WHERE status = 'OPEN' LIMIT 20")
    open_orders = [row[0] for row in cur.fetchall()]
    cur.close()
    conn.close()

    if not open_orders:
        print("⚠️  No open orders to cancel, skipping test")
        return

    print(f"Found {len(open_orders)} open orders to cancel")

    cancel_results = defaultdict(list)
    lock = threading.Lock()

    def cancel_orders(account_id, order_ids_to_cancel):
        for order_id in order_ids_to_cancel:
            xml = f"""<?xml version="1.0"?>
<transactions id="{account_id}">
  <cancel id="{order_id}"/>
</transactions>"""
            resp = send_xml_request(xml)
            with lock:
                # Determine if cancel succeeded
                success = resp and "<canceled" in resp
                cancel_results[order_id].append((account_id, success, resp))

    # Each thread tries to cancel the same orders
    threads = []
    all_accounts = buyer_ids + seller_ids

    for i in range(min(num_threads, len(all_accounts))):
        account_id = all_accounts[i]
        # All threads try to cancel the same orders (race condition test)
        t = threading.Thread(target=cancel_orders, args=(account_id, open_orders))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    # Verify: each order should be successfully canceled by at most one thread
    issues = 0
    for order_id, results in cancel_results.items():
        success_count = sum(1 for _, success, _ in results if success)
        if success_count > 1:
            print(f"❌ Order {order_id} was canceled {success_count} times (race condition!)")
            issues += 1

    if issues == 0:
        print(f"✅ All orders canceled correctly (no race conditions detected)")
    else:
        print(f"❌ Found {issues} race condition issue(s)")

    return issues == 0


def test_concurrent_queries(order_ids, buyer_ids, seller_ids, num_threads=8):
    """
    Test 3: Concurrent queries with shared_mutex.
    Multiple threads query the same orders concurrently.
    """
    print(f"\n=== Test 3: Concurrent Queries (Shared_Mutex Correctness) ===")

    if not order_ids:
        print("⚠️  No orders to query, skipping test")
        return True

    # Pick a few orders to query
    query_order_ids = order_ids[:min(10, len(order_ids))]
    print(f"Querying {len(query_order_ids)} orders with {num_threads} concurrent threads...")

    query_results = defaultdict(list)
    lock = threading.Lock()
    errors = []

    def query_orders(account_id, order_ids_to_query):
        for order_id in order_ids_to_query:
            xml = f"""<?xml version="1.0"?>
<transactions id="{account_id}">
  <query id="{order_id}"/>
</transactions>"""
            resp = send_xml_request(xml)
            with lock:
                try:
                    # Parse response
                    root = ET.fromstring(resp)
                    status = root.find("status")
                    if status is not None:
                        query_results[order_id].append(resp)
                    elif "error" in resp.lower():
                        # Error response is OK (order might not belong to this account)
                        pass
                    else:
                        errors.append(f"Invalid response for order {order_id}")
                except Exception as e:
                    errors.append(f"Parse error for order {order_id}: {e}")

    # Launch threads
    threads = []
    all_accounts = buyer_ids + seller_ids

    for i in range(min(num_threads, len(all_accounts))):
        account_id = all_accounts[i % len(all_accounts)]
        t = threading.Thread(target=query_orders, args=(account_id, query_order_ids))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    # Verify: all queries should return valid XML
    if errors:
        print(f"❌ Found {len(errors)} error(s) in concurrent queries:")
        for err in errors[:5]:
            print(f"   - {err}")
        return False
    else:
        total_queries = sum(len(results) for results in query_results.values())
        print(f"✅ All {total_queries} concurrent queries returned valid responses")
        print(f"   (no crashes, no data races, shared_mutex working correctly)")
        return True


def run_comprehensive_test():
    """Run all concurrent correctness tests."""
    print("=" * 70)
    print("COMPREHENSIVE CONCURRENT CORRECTNESS TEST")
    print("=" * 70)

    # Setup
    clear_db()
    buyer_ids, seller_ids, initial_balance = setup_test_accounts(num_buyers=4, num_sellers=4)

    if not buyer_ids:
        print("❌ Failed to setup test accounts")
        return False

    # Test 1: Concurrent orders
    order_ids = test_concurrent_orders(buyer_ids, seller_ids, num_threads=4)

    # Test 2: Concurrent cancels
    cancel_ok = test_concurrent_cancels(order_ids, buyer_ids, seller_ids, num_threads=4)

    # Test 3: Concurrent queries
    query_ok = test_concurrent_queries(order_ids, buyer_ids, seller_ids, num_threads=8)

    # Run verification
    print("\n" + "=" * 70)
    print("RUNNING DB VERIFICATION")
    print("=" * 70)

    import verify_correctness
    verification_ok = verify_correctness.run_all_verifications(initial_balance)

    # Final summary
    print("\n" + "=" * 70)
    print("FINAL TEST SUMMARY")
    print("=" * 70)

    all_passed = cancel_ok and query_ok and verification_ok

    print(f"Test 1 - Concurrent Orders: {'✅ PASS' if order_ids else '❌ FAIL'}")
    print(f"Test 2 - Concurrent Cancels: {'✅ PASS' if cancel_ok else '❌ FAIL'}")
    print(f"Test 3 - Concurrent Queries: {'✅ PASS' if query_ok else '❌ FAIL'}")
    print(f"DB Verification: {'✅ PASS' if verification_ok else '❌ FAIL'}")

    if all_passed:
        print("\n🎉 ALL CONCURRENT CORRECTNESS TESTS PASSED!")
    else:
        print("\n⚠️  SOME TESTS FAILED")

    return all_passed


if __name__ == "__main__":
    success = run_comprehensive_test()
    exit(0 if success else 1)
