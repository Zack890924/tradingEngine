import socket
import struct
import sys

def send_xml_request(xml_str):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            print("Connecting...")
            s.connect(("localhost", 12345))
            s.settimeout(5.0)
            
            print("Sending request...")
            xml_bytes = xml_str.encode('utf-8')
            s.sendall(struct.pack("!I", len(xml_bytes)) + xml_bytes)
            
            print("Waiting for response...")
            raw_len = s.recv(4)
            if len(raw_len) != 4:
                print(f"ERROR: Received {len(raw_len)} bytes, expected 4")
                return None
                
            msg_len = struct.unpack("!I", raw_len)[0]
            print(f"Expecting {msg_len} bytes in response")
            
            data = b''
            while len(data) < msg_len:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
            
            return data.decode()
    except Exception as e:
        print(f"ERROR: {e}")
        return None

# Very minimal test
xml = '''<?xml version="1.0"?>
<create>
</create>'''

print("Testing server connection...")
response = send_xml_request(xml)
if response:
    print("\nResponse:")
    print(response)
else:
    print("\nNo response received")
    sys.exit(1)