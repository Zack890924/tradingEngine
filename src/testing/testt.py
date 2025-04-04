#!/usr/bin/env python3
import socket
import struct
import time
import re
import xml.etree.ElementTree as ET
import psycopg2
import uuid

HOST = 'localhost'
SERVER_PORT = 12345


DB_HOST = 'localhost'
DB_PORT = 5433  
DB_USER = 'exchange'
DB_PASSWORD = 'exchange_password'
DB_NAME = 'exchange_db'


global_ids = {}

global_order_ids = {}

def unique_account(prefix):
    return f"{prefix}_{uuid.uuid4().hex[:6]}"

def send_xml_request(xml_str):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((HOST, SERVER_PORT))
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
        return True, ""
    except Exception as e:
        return False, str(e)

def run_server_tests():
   
    acct_buy = unique_account("acct_buy")      
    acct_sell = unique_account("acct_sell")    
    acct_dup = unique_account("acct_dup")      
    acct_symbol_missing = unique_account("acct_missing")  
    acct_insufficient_funds = unique_account("acct_fund")   
    acct_insufficient_shares = unique_account("acct_shares")
    acct_invalid = unique_account("acct_invalid")          
    acct_match_buy = unique_account("acct_match_buy")       
    acct_match_sell = unique_account("acct_match_sell")   


    global_ids["buy"] = acct_buy
    global_ids["sell"] = acct_sell
    global_ids["dup"] = acct_dup
    global_ids["missing"] = acct_symbol_missing
    global_ids["insufficient_funds"] = acct_insufficient_funds
    global_ids["insufficient_shares"] = acct_insufficient_shares
    global_ids["invalid"] = acct_invalid
    global_ids["match_buy"] = acct_match_buy
    global_ids["match_sell"] = acct_match_sell

    
    test_cases = [
      
        ("Test 1: Create account and symbol for buy order", 
         f"""<?xml version="1.0"?>
<create>
  <account id="{acct_buy}" balance="10000"/>
  <symbol sym="AAPL">
    <account id="{acct_buy}">100</account>
  </symbol>
</create>"""),

       
        ("Test 2: Create account and symbol for sell order", 
         f"""<?xml version="1.0"?>
<create>
  <account id="{acct_sell}" balance="15000"/>
  <symbol sym="AAPL">
    <account id="{acct_sell}">200</account>
  </symbol>
</create>"""),

        ("Test 3: Duplicate account creation", 
         f"""<?xml version="1.0"?>
<create>
  <account id="{acct_dup}" balance="5000"/>
  <account id="{acct_dup}" balance="8000"/>
</create>"""),

    
        ("Test 4: Symbol with missing account id", 
         f"""<?xml version="1.0"?>
<create>
  <account id="{acct_symbol_missing}" balance="7000"/>
  <symbol sym="GOOG">
    <account id="{acct_symbol_missing}">300</account>
    <account>150</account>
  </symbol>
</create>"""),

 
        ("Test 5: Valid buy order", 
         f"""<?xml version="1.0"?>
<transactions id="{acct_buy}">
  <order sym="AAPL" amount="50" limit="120"/>
</transactions>"""),


        ("Test 6: Valid sell order", 
         f"""<?xml version="1.0"?>
<transactions id="{acct_sell}">
  <order sym="AAPL" amount="-30" limit="115"/>
</transactions>"""),


        ("Test 7: Query buy order", 
         lambda: f"""<?xml version="1.0"?>
<transactions id="{acct_buy}">
  <query id="{global_order_ids.get('buy', '0')}"/>
</transactions>"""),

        ("Test 8: Cancel buy order", 
         lambda: f"""<?xml version="1.0"?>
<transactions id="{acct_buy}">
  <cancel id="{global_order_ids.get('buy', '0')}"/>
</transactions>"""),


        ("Test 9: Buy order with insufficient funds", 
         f"""<?xml version="1.0"?>
<transactions id="{acct_insufficient_funds}">
  <order sym="AAPL" amount="1000" limit="150"/>
</transactions>"""),

   
        ("Test 10: Sell order with insufficient shares", 
         f"""<?xml version="1.0"?>
<transactions id="{acct_insufficient_shares}">
  <order sym="AAPL" amount="-500" limit="110"/>
</transactions>"""),

        ("Test 11: Query non-existent order", 
         f"""<?xml version="1.0"?>
<transactions id="{acct_buy}">
  <query id="9999"/>
</transactions>"""),

        ("Test 12: Malformed XML", 
         """<?xml version="1.0"?>
<create><account id="707" balance="3000"
"""),


        ("Test 13: Transactions with invalid account", 
         f"""<?xml version="1.0"?>
<transactions id="{acct_invalid}">
  <order sym="MSFT" amount="100" limit="200"/>
  <query id="10"/>
  <cancel id="10"/>
</transactions>"""),


        ("Test 14a: Create account for matching (buy side)", 
         f"""<?xml version="1.0"?>
<create>
  <account id="{acct_match_buy}" balance="20000"/>
  <symbol sym="AAPL">
    <account id="{acct_match_buy}">500</account>
  </symbol>
</create>"""),
        ("Test 14b: Create account for matching (sell side)", 
         f"""<?xml version="1.0"?>
<create>
  <account id="{acct_match_sell}" balance="20000"/>
  <symbol sym="AAPL">
    <account id="{acct_match_sell}">500</account>
  </symbol>
</create>"""),

        ("Test 14c: Matching orders with partial fill", 
         f"""<?xml version="1.0"?>
<transactions id="{acct_match_buy}">
  <order sym="AAPL" amount="100" limit="125"/>
</transactions>
<transactions id="{acct_match_sell}">
  <order sym="AAPL" amount="-50" limit="120"/>
</transactions>
<transactions id="{acct_match_sell}">
  <order sym="AAPL" amount="-50" limit="123"/>
</transactions>
<transactions id="{acct_match_buy}">
  <query id="2"/>
</transactions>"""),
    ]

    print("==== Running Server Tests ====")
    for name, xml in test_cases:
        print(f"\n==== {name} ====")
        xml_str = xml() if callable(xml) else xml
        xml_str = xml_str.strip()
        if not xml_str.startswith("<?xml"):
            xml_str = "<?xml version=\"1.0\"?>\n" + xml_str

        # 檢查 XML 格式
        is_valid, err_msg = check_xml_format(xml_str)
        if not is_valid:
            print("XML Format Error:", err_msg)
        else:
            print("XML Format OK.")

        response = send_xml_request(xml_str)
        print("Response:")
        print(response if response else "No response")

      
        if "Valid buy order" in name and response:
            match = re.search(r'<opened .*?id="(\d+)"', response)
            if match:
                order_id = match.group(1)
                global_order_ids["buy"] = order_id
                print(f"Captured buy order id: {order_id}")
            else:
                print("Buy order id not captured.")
        time.sleep(0.2)

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
    run_server_tests()
    time.sleep(1)
    check_db_credentials()
    check_database_population()
