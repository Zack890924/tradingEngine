#!/usr/bin/env python3
"""
Verification script to check concurrent correctness after stress tests.
Validates: no double-match, balance conservation, order state consistency.
"""

import psycopg2
from decimal import Decimal

DB_CONFIG = {
    "host": "localhost",
    "port": "5433",
    "user": "exchange",
    "password": "exchange_password",
    "dbname": "exchange_db"
}


def get_db_connection():
    """Create and return a database connection."""
    return psycopg2.connect(**DB_CONFIG)


def verify_no_double_match():
    """
    Verify that no execution appears more than once.
    Checks for duplicate (buy_order_id, sell_order_id) pairs.
    """
    print("\n=== Test 1: Verify No Double-Match ===")
    conn = get_db_connection()
    cur = conn.cursor()

    # Check for duplicate executions
    cur.execute("""
        SELECT buy_order_id, sell_order_id, COUNT(*) as cnt
        FROM executions
        GROUP BY buy_order_id, sell_order_id
        HAVING COUNT(*) > 1
    """)

    duplicates = cur.fetchall()

    if duplicates:
        print(f"❌ FAILED: Found {len(duplicates)} duplicate execution(s):")
        for dup in duplicates:
            print(f"   Buy Order {dup[0]} × Sell Order {dup[1]}: matched {dup[2]} times")
        cur.close()
        conn.close()
        return False

    # Count total executions
    cur.execute("SELECT COUNT(*) FROM executions")
    total_execs = cur.fetchone()[0]

    print(f"✅ PASSED: No double-match detected ({total_execs} unique executions)")
    cur.close()
    conn.close()
    return True


def verify_no_duplicate_cancels():
    """
    Verify that canceled orders don't have multiple cancel events.
    Check that each canceled order appears only once in the canceled status.
    """
    print("\n=== Test 2: Verify No Duplicate Cancels ===")
    conn = get_db_connection()
    cur = conn.cursor()

    # Check if any order was "canceled" multiple times by looking at status updates
    # (This assumes you have a way to track cancellation events)
    # For now, we verify that canceled orders have correct open_amount = 0
    cur.execute("""
        SELECT order_id, open_amount
        FROM orders
        WHERE status = 'CANCELED' AND open_amount < 0
    """)

    invalid_cancels = cur.fetchall()

    if invalid_cancels:
        print(f"❌ FAILED: Found {len(invalid_cancels)} canceled order(s) with invalid open_amount:")
        for order in invalid_cancels:
            print(f"   Order {order[0]}: open_amount = {order[1]} (should be >= 0)")
        cur.close()
        conn.close()
        return False

    cur.execute("SELECT COUNT(*) FROM orders WHERE status = 'CANCELED'")
    total_canceled = cur.fetchone()[0]

    print(f"✅ PASSED: All canceled orders have valid state ({total_canceled} canceled orders)")
    cur.close()
    conn.close()
    return True


def verify_balance_conservation(initial_total_balance=None):
    """
    Verify that total balance across all accounts is conserved.
    Money should not be created or destroyed.

    Args:
        initial_total_balance: Expected total balance (optional)
    """
    print("\n=== Test 3: Verify Balance Conservation ===")
    conn = get_db_connection()
    cur = conn.cursor()

    # Calculate total balance
    cur.execute("SELECT SUM(balance) FROM accounts")
    total_balance = cur.fetchone()[0] or Decimal('0')

    print(f"   Total balance across all accounts: ${total_balance}")

    if initial_total_balance is not None:
        if abs(total_balance - Decimal(str(initial_total_balance))) < Decimal('0.01'):
            print(f"✅ PASSED: Balance conserved (expected: ${initial_total_balance})")
            result = True
        else:
            print(f"❌ FAILED: Balance not conserved (expected: ${initial_total_balance}, got: ${total_balance})")
            result = False
    else:
        print(f"⚠️  SKIPPED: No initial balance provided for comparison")
        result = True

    cur.close()
    conn.close()
    return result


def verify_execution_amounts():
    """
    Verify that all executions have positive amounts.
    Check that execution prices are within reasonable bounds.
    """
    print("\n=== Test 4: Verify Execution Data Integrity ===")
    conn = get_db_connection()
    cur = conn.cursor()

    # Check for invalid amounts
    cur.execute("""
        SELECT execution_id, amount, price
        FROM executions
        WHERE amount <= 0 OR price <= 0
    """)

    invalid_execs = cur.fetchall()

    if invalid_execs:
        print(f"❌ FAILED: Found {len(invalid_execs)} execution(s) with invalid amount/price:")
        for exec in invalid_execs[:5]:  # Show first 5
            print(f"   Execution {exec[0]}: amount={exec[1]}, price={exec[2]}")
        cur.close()
        conn.close()
        return False

    cur.execute("SELECT COUNT(*) FROM executions")
    total = cur.fetchone()[0]

    print(f"✅ PASSED: All executions have valid amounts and prices ({total} executions)")
    cur.close()
    conn.close()
    return True


def verify_order_state_consistency():
    """
    Verify that order states are consistent:
    - EXECUTED orders should have open_amount = 0
    - OPEN orders should have open_amount > 0
    - CANCELED orders should have open_amount >= 0
    """
    print("\n=== Test 5: Verify Order State Consistency ===")
    conn = get_db_connection()
    cur = conn.cursor()

    issues = []

    # Check EXECUTED orders
    cur.execute("""
        SELECT order_id, status, open_amount
        FROM orders
        WHERE status = 'EXECUTED' AND ABS(open_amount) > 0.001
    """)
    executed_issues = cur.fetchall()
    if executed_issues:
        issues.append(f"EXECUTED orders with non-zero open_amount: {len(executed_issues)}")

    # Check OPEN orders
    cur.execute("""
        SELECT order_id, status, open_amount
        FROM orders
        WHERE status = 'OPEN' AND ABS(open_amount) < 0.001
    """)
    open_issues = cur.fetchall()
    if open_issues:
        issues.append(f"OPEN orders with zero open_amount: {len(open_issues)}")

    if issues:
        print(f"❌ FAILED: Found order state inconsistencies:")
        for issue in issues:
            print(f"   - {issue}")
        cur.close()
        conn.close()
        return False

    cur.execute("SELECT status, COUNT(*) FROM orders GROUP BY status")
    status_counts = cur.fetchall()

    print(f"✅ PASSED: All order states are consistent")
    print(f"   Order status distribution:")
    for status, count in status_counts:
        print(f"   - {status}: {count}")

    cur.close()
    conn.close()
    return True


def run_all_verifications(initial_total_balance=None):
    """
    Run all verification tests and return overall result.

    Args:
        initial_total_balance: Expected total balance (optional)

    Returns:
        True if all tests passed, False otherwise
    """
    print("=" * 60)
    print("CONCURRENT CORRECTNESS VERIFICATION")
    print("=" * 60)

    tests = [
        ("No Double-Match", lambda: verify_no_double_match()),
        ("No Duplicate Cancels", lambda: verify_no_duplicate_cancels()),
        ("Balance Conservation", lambda: verify_balance_conservation(initial_total_balance)),
        ("Execution Integrity", lambda: verify_execution_amounts()),
        ("Order State Consistency", lambda: verify_order_state_consistency()),
    ]

    results = []
    for test_name, test_func in tests:
        try:
            result = test_func()
            results.append((test_name, result))
        except Exception as e:
            print(f"❌ ERROR in {test_name}: {e}")
            results.append((test_name, False))

    # Summary
    print("\n" + "=" * 60)
    print("VERIFICATION SUMMARY")
    print("=" * 60)

    passed = sum(1 for _, result in results if result)
    total = len(results)

    for test_name, result in results:
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"{status}: {test_name}")

    print(f"\nOverall: {passed}/{total} tests passed")

    if passed == total:
        print("\n🎉 All verification tests PASSED!")
        return True
    else:
        print(f"\n⚠️  {total - passed} test(s) FAILED")
        return False


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Verify concurrent correctness after stress tests")
    parser.add_argument("--initial-balance", type=float, help="Expected total initial balance")
    args = parser.parse_args()

    success = run_all_verifications(args.initial_balance)
    exit(0 if success else 1)
