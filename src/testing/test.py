import socket
import struct
import time

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

order_ids = {}
tests = [
    ("Create Account 101 + Symbol AAPL", """
    <create>
        <account id="101" balance="10000"/>
        <symbol sym="AAPL">
            <account id="101">100</account>
        </symbol>
    </create>
    """),

    ("Create Account 202 + Symbol AAPL", """
    <create>
        <account id="202" balance="10000"/>
        <symbol sym="AAPL">
            <account id="202">100</account>
        </symbol>
    </create>
    """),

    ("Valid Buy Order by 101", """
    <transactions id="101">
        <order sym="AAPL" amount="10" limit="150"/>
    </transactions>
    """),

    ("Valid Sell Order by 202", """
    <transactions id="202">
        <order sym="AAPL" amount="-5" limit="145"/>
    </transactions>
    """),

    ("Invalid Symbol Order", """
    <transactions id="101">
        <order sym="NOTEXIST" amount="5" limit="120"/>
    </transactions>
    """),

    ("Invalid Account Order", """
    <transactions id="999">
        <order sym="AAPL" amount="5" limit="100"/>
    </transactions>
    """),

    ("Query Buy Order (later updated)", lambda: f"""
    <transactions id="101">
        <query id="{order_ids.get('buy', '1')}"/>
    </transactions>
    """),

    ("Cancel Buy Order", lambda: f"""
    <transactions id="101">
        <cancel id="{order_ids.get('buy', '1')}"/>
    </transactions>
    """),

    ("Cancel Non-existent Order", """
    <transactions id="101">
        <cancel id="9999"/>
    </transactions>
    """),
]

if __name__ == "__main__":
    for idx, (name, xml) in enumerate(tests, 1):
        print(f"\n==== Test {idx}: {name} ====")
        if callable(xml):
            xml_str = xml()
        else:
            xml_str = f'<?xml version="1.0"?>\n{xml.strip()}\n'

        response = send_xml_request(xml_str)
        print(response if response else "No response")

        if response and "<opened" in response:
            import re
            match = re.search(r'id="(\d+)"', response)
            if match:
                order_id = match.group(1)
                if 'Test 3' in name:  # Buy order test
                    order_ids['buy'] = order_id
                    print(f"Captured buy order ID: {order_id}")
                elif 'Test 4' in name:  # Sell order test  
                    order_ids['sell'] = order_id
                    print(f"Captured sell order ID: {order_id}")

        time.sleep(0.2)
