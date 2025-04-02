import socket
import struct
import time
import xml.etree.ElementTree as ET
from hashlib import md5
import psycopg2

HOST = 'localhost'
PORT = 12345

# Database settings for credential and population check
DB_HOST = 'localhost'
DB_PORT = 5433
DB_USER = 'exchange'
DB_PASSWORD = 'exchange_password'
DB_NAME = 'exchange_db'

# Generate stable account IDs
account_ids = {
    "buy": "acct_buy_72d271",
    "sell": "acct_sell_7ac2a1",
    "duplicate": "acct_dup_fde4ed",
    "missing": "acct_missing_4ec917",
    "insufficient_funds": "nonexistent_fund",
    "insufficient_shares": "nonexistent_shares",
    "invalid": "acct_invalid_235f0a",
    "match_buy": "acct_match_buy_51e536",
    "match_sell": "acct_match_sell_c17ae2",
}

buy_order_id = None


def send_xml_request(xml_str):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((HOST, PORT))
            xml_bytes = xml_str.encode('utf-8')
            s.sendall(struct.pack('!I', len(xml_bytes)) + xml_bytes)

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
        return f"Connection error: {e}"


def check_xml_format(xml_str):
    try:
        ET.fromstring(xml_str)
        return "XML Format OK."
    except ET.ParseError as e:
        return f"XML Format Error: {e}"


def create_account_xml(account_id, balance):
    return f"""<?xml version=\"1.0\"?>
<create>
  <account id=\"{account_id}\" balance=\"{balance}\"/>
  <symbol sym=\"AAPL\">
    <account id=\"{account_id}\">100</account>
  </symbol>
</create>"""


def check_db_credentials():
    print("\n==== Checking Database Credentials ====")
    try:
        conn = psycopg2.connect(
            host=DB_HOST,
            port=DB_PORT,
            user=DB_USER,
            password=DB_PASSWORD,
            dbname=DB_NAME
        )
        print("Database connection successful using provided credentials.")
        conn.close()
    except Exception as e:
        print("Database connection failed:", e)


def check_database_population():
    print("\n==== Checking Database Population ====")
    try:
        conn = psycopg2.connect(
            host=DB_HOST,
            port=DB_PORT,
            user=DB_USER,
            password=DB_PASSWORD,
            dbname=DB_NAME
        )
        cur = conn.cursor()
        tables = ["accounts", "symbols", "orders", "executions"]
        for table in tables:
            cur.execute(f"SELECT COUNT(*) FROM {table};")
            count = cur.fetchone()[0]
            print(f"Table '{table}': {count} rows")
        conn.close()
    except Exception as e:
        print("Database connection error:", e)


if __name__ == "__main__":
    print("==== Running Server Tests ====")
    tests = []

    tests.append((f"Test 1: Create account and symbol for buy order",
                  create_account_xml(account_ids["buy"], 20000)))

    tests.append((f"Test 2: Create account and symbol for sell order",
                  create_account_xml(account_ids["sell"], 10000)))

    tests.append((f"Test 3: Duplicate account creation",
                  f"""<?xml version=\"1.0\"?>
<create>
  <account id=\"{account_ids['duplicate']}\" balance=\"10000\"/>
  <account id=\"{account_ids['duplicate']}\" balance=\"10000\"/>
</create>"""))

    tests.append((f"Test 4: Symbol with missing account id",
                  f"""<?xml version=\"1.0\"?>
<create>
  <account id=\"{account_ids['missing']}\" balance=\"10000\"/>
  <symbol sym=\"GOOG\">
    <account id=\"{account_ids['missing']}\">200</account>
    <account>100</account>
  </symbol>
</create>"""))

    tests.append(("Test 5: Valid buy order for AAPL by account",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['buy']}\">
  <order sym=\"AAPL\" amount=\"50\" limit=\"120\"/>
</transactions>"""))

    tests.append(("Test 6: Valid sell order for AAPL by account",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['sell']}\">
  <order sym=\"AAPL\" amount=\"-30\" limit=\"115\"/>
</transactions>"""))

    tests.append(("Test 9: Buy with insufficient funds",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['insufficient_funds']}\">
  <order sym=\"AAPL\" amount=\"500\" limit=\"500\"/>
</transactions>"""))

    tests.append(("Test 10: Sell with insufficient shares",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['insufficient_shares']}\">
  <order sym=\"AAPL\" amount=\"-1000\" limit=\"100\"/>
</transactions>"""))

    tests.append(("Test 11: Query non-existent order",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['buy']}\">
  <query id=\"9999\"/>
</transactions>"""))

    tests.append(("Test 12: Malformed XML",
                  f"<?xml version=\"1.0\"?><create><account id=\"test\" balance=\"1000\""))

    tests.append(("Test 13: Transactions with invalid account",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['invalid']}\">
  <order sym=\"AAPL\" amount=\"100\" limit=\"123\"/>
</transactions>"""))

    tests.append(("Test 14a: Create account for matching (buy side)",
                  create_account_xml(account_ids["match_buy"], 20000)))

    tests.append(("Test 14b: Create account for matching (sell side)",
                  create_account_xml(account_ids["match_sell"], 10000)))

    # Three separate XMLs for valid matching orders and query
    tests.append(("Test 14c-1: Buy order for matching",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['match_buy']}\">
  <order sym=\"AAPL\" amount=\"100\" limit=\"125\"/>
</transactions>"""))

    tests.append(("Test 14c-2: Sell order for matching",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['match_sell']}\">
  <order sym=\"AAPL\" amount=\"-50\" limit=\"120\"/>
</transactions>"""))

    tests.append(("Test 14c-3: Sell order for matching",
                  f"""<?xml version=\"1.0\"?>
<transactions id=\"{account_ids['match_sell']}\">
  <order sym=\"AAPL\" amount=\"-50\" limit=\"123\"/>
</transactions>"""))

    for i, (desc, xml) in enumerate(tests, 1):
        print(f"\n==== {desc} ====")
        print(check_xml_format(xml))
        response = send_xml_request(xml)
        print("Response:")
        print(response if response else "No response")
        time.sleep(0.2)

    check_db_credentials()
    check_database_population()
