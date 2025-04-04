import socket
import struct
import time
import xml.etree.ElementTree as ET
import psycopg2


def clear_test_data():
    conn = psycopg2.connect(
        dbname="exchange_db",
        user="exchange",
        password="exchange_password",
        host="localhost",
        port="5433"
    )
    conn.autocommit = True
    cur = conn.cursor()
    try:
        cur.execute("DELETE FROM executions;")
        cur.execute("DELETE FROM orders;")
        cur.execute("DELETE FROM positions;")
        cur.execute("DELETE FROM accounts;")
        print("Cleared test data in PostgreSQL")
    except Exception as e:
        print("Failed to clear test data:", e)
    finally:
        cur.close()
        conn.close()


HOST = 'localhost'
PORT = 12345

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



def create_accounts(buyer_id, seller_id, symbol):
    return f"""<?xml version="1.0"?>
<create>
  <account id="{buyer_id}" balance="30000"/>
  <account id="{seller_id}" balance="5000"/>
  <symbol sym="{symbol}">
    <account id="{seller_id}">100</account>
  </symbol>
</create>"""

def place_buy_order(account_id, symbol, amount, limit):
    return f"""<?xml version="1.0"?>
<transactions id="{account_id}">
  <order sym="{symbol}" amount="{amount}" limit="{limit}"/>
</transactions>"""

def place_sell_order(account_id, symbol, amount, limit):
    return f"""<?xml version="1.0"?>
<transactions id="{account_id}">
  <order sym="{symbol}" amount="{amount}" limit="{limit}"/>
</transactions>"""

def query_order(account_id, order_id):
    return f"""<?xml version="1.0"?>
<transactions id="{account_id}">
  <query id="{order_id}"/>
</transactions>"""

def cancel_order(account_id, order_id):
    return f"""<?xml version="1.0"?>
<transactions id="{account_id}">
  <cancel id="{order_id}"/>
</transactions>"""


def query_executions():
    try:
        conn = psycopg2.connect(
            host="localhost", port="5433", user="exchange",
            password="exchange_password", dbname="exchange_db"
        )
        cur = conn.cursor()
        cur.execute("SELECT execution_id, buy_order_id, sell_order_id, symbol, amount, price, executed_at FROM executions ORDER BY execution_id DESC;")
        rows = cur.fetchall()
        if rows:
            print("\nExecutions in DB:")
            for row in rows:
                print(f"  Execution ID: {row[0]}, Buy Order ID: {row[1]}, Sell Order ID: {row[2]}, Symbol: {row[3]}, Amount: {row[4]}, Price: {row[5]}, Time: {row[6]}")
        else:
            print("No execution found in database.")
        conn.close()
    except Exception as e:
        print("DB query failed:", e)


if __name__ == "__main__":
    print("==== Running Full Trading Engine Test ====")
    clear_test_data()

    
    timestamp = int(time.time())
    buyer_id = f"acct_test_buy_{timestamp}"
    seller_id = f"acct_test_sell_{timestamp}"
    symbol = "AAPL"


    print("\n==== Step 1: Create Accounts and Symbol ====")
    xml = create_accounts(buyer_id, seller_id, symbol)
    print(check_xml_format(xml))
    resp = send_xml_request(xml)
    print("Response:")
    print(resp)
    time.sleep(0.5)


    print("\n==== Step 2: Place Buy Order (Full Match) ====")
    xml = place_buy_order(buyer_id, symbol, 40, 125) 
    print(check_xml_format(xml))
    resp = send_xml_request(xml)
    print("Response:")
    print(resp)
    time.sleep(0.5)
    buy_order_id = None
    try:
        root = ET.fromstring(resp)
        opened_elem = root.find("opened")
        if opened_elem is not None:
            buy_order_id = opened_elem.attrib["id"]
            print(f"\nExtracted Buy Order ID: {buy_order_id}")
        else:
            print("\nBuy order not opened; response:", ET.tostring(root, encoding="unicode"))
    except Exception as e:
        print("Error extracting Buy Order ID:", e)

    
    print("\n==== Step 3: Place Sell Order (Full Match) ====")
    xml = place_sell_order(seller_id, symbol, -40, 120) 
    print(check_xml_format(xml))
    resp = send_xml_request(xml)
    print("Response:")
    print(resp)
    time.sleep(0.5)

    print("\n==== Step 4: Query Buy Order ====")
    if buy_order_id:
        xml = query_order(buyer_id, buy_order_id)
        print("Query XML:")
        print(xml)
        resp = send_xml_request(xml)
        print("Response:")
        print(resp)
    else:
        print("❗ Could not extract Buy Order ID for query.")
    time.sleep(0.5)


    print("\n==== Step 5: Confirm Executions in DB ====")
    query_executions()
    time.sleep(0.5)


    print("\n==== Step 6: Partial Match Test ====")

    xml = place_buy_order(buyer_id, symbol, 100, 130)
    print("Placing large buy order:")
    print(check_xml_format(xml))
    resp = send_xml_request(xml)
    print("Response:")
    print(resp)
    time.sleep(0.5)
    large_buy_order_id = None
    try:
        root = ET.fromstring(resp)
        opened_elem = root.find("opened")
        if opened_elem is not None:
            large_buy_order_id = opened_elem.attrib["id"]
            print(f"\nExtracted Large Buy Order ID: {large_buy_order_id}")
        else:
            print("\n❗ Large Buy order not opened; response:", ET.tostring(root, encoding="unicode"))
    except Exception as e:
        print("❗ Error extracting Large Buy Order ID:", e)

 
    print("\nPlacing first sell order for 40 shares:")
    xml = place_sell_order(seller_id, symbol, -40, 125)
    print(check_xml_format(xml))
    resp = send_xml_request(xml)
    print("Response:")
    print(resp)
    time.sleep(0.5)
    
    print("\nPlacing second sell order for 60 shares:")
    xml = place_sell_order(seller_id, symbol, -60, 124)
    print(check_xml_format(xml))
    resp = send_xml_request(xml)
    print("Response:")
    print(resp)
    time.sleep(1)

 
    if large_buy_order_id:
        print("\nQuerying large buy order status:")
        xml = query_order(buyer_id, large_buy_order_id)
        print("Query XML:")
        print(xml)
        resp = send_xml_request(xml)
        print("Response:")
        print(resp)
    else:
        print("❗ Could not extract Large Buy Order ID for query.")
    time.sleep(0.5)


    print("\n==== Step 7: Cancel Remaining Order ====")
 
    if large_buy_order_id:
        xml = cancel_order(buyer_id, large_buy_order_id)
        print("Cancel order XML:")
        print(xml)
        resp = send_xml_request(xml)
        print("Response:")
        print(resp)
    else:
        print("❗ No large buy order to cancel.")
    time.sleep(0.5)

    # Step 8: 
    print("\n==== Step 8: Insufficient Funds Test ====")

    invalid_buyer = "nonexistent_account"
    xml = place_buy_order(invalid_buyer, symbol, 50, 200)
    print("Placing buy order with invalid account:")
    print(check_xml_format(xml))
    resp = send_xml_request(xml)
    print("Response:")
    print(resp)
    time.sleep(0.5)

   
    print("\n==== Step 9: Invalid XML Test ====")
    invalid_xml = "<transactions id='test'><order sym='AAPL' amount='40' limit='125'"  
    print("Invalid XML:")
    print(invalid_xml)
    resp = send_xml_request(invalid_xml)
    print("Response:")
    print(resp)

    print("\n==== All Tests Completed ====")
