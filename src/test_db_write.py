import socket
import struct
import psycopg2
import time
import xml.etree.ElementTree as ET

def get_db_connection():
    """Get a connection to the database - try multiple methods"""
    # Method 1: Try to get the container IP dynamically
    try:
        import subprocess
        output = subprocess.check_output(
            ["docker", "inspect", "-f", "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}", 
             "erss-hwk4-ar791-yc658_postgres_1"], 
            text=True
        ).strip()
        
        if output:
            print(f"Found PostgreSQL container IP: {output}")
            try:
                conn = psycopg2.connect(
                    host=output,
                    database="exchange_db",
                    user="exchange",
                    password="exchange_pass",
                    connect_timeout=3
                )
                print("✅ Connected to database using container IP")
                return conn
            except Exception as e:
                print(f"Could not connect using container IP: {e}")
    except Exception as e:
        print(f"Could not determine container IP: {e}")
    
    # Method 2: Try the hostname method (works from within Docker network)
    try:
        conn = psycopg2.connect(
            host="postgres",
            database="exchange_db",
            user="exchange",
            password="exchange_pass",
            connect_timeout=3
        )
        print("✅ Connected to database using hostname 'postgres'")
        return conn
    except Exception as e:
        print(f"Could not connect using hostname: {e}")
    
    # Method 3: Try localhost with port mapping (assuming port is mapped in docker-compose.yml)
    try:
        conn = psycopg2.connect(
            host="localhost",
            port=5432,  # Default PostgreSQL port, might be mapped differently
            database="exchange_db",
            user="exchange",
            password="exchange_pass",
            connect_timeout=3
        )
        print("✅ Connected to database using localhost")
        return conn
    except Exception as e:
        print(f"Could not connect using localhost: {e}")
    
    # No connection method worked
    print("❌ Failed to connect to the database using any method")
    return None

def clear_test_data():
    """Clear test data from the database"""
    conn = get_db_connection()
    if not conn:
        return
        
    try:
        with conn.cursor() as cur:
            print("Clearing test data from database...")
            
            # Delete test accounts and related data
            cur.execute("DELETE FROM positions WHERE account_id LIKE 'test%'")
            cur.execute("DELETE FROM orders WHERE account_id LIKE 'test%'")
            cur.execute("DELETE FROM accounts WHERE account_id LIKE 'test%'")
            
            conn.commit()
            print("Test data cleared")
    except Exception as e:
        print(f"Error clearing test data: {e}")
    finally:
        conn.close()

def check_account_in_db(account_id):
    """Check if an account exists in the database"""
    conn = get_db_connection()
    if not conn:
        return False
        
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT * FROM accounts WHERE account_id = %s", (account_id,))
            account = cur.fetchone()
            if account:
                print(f"✅ Found account in database: {account}")
                return True
            else:
                print(f"❌ Account not found in database: {account_id}")
                return False
    except Exception as e:
        print(f"Error checking account: {e}")
        return False
    finally:
        conn.close()

def send_request(xml_str):
    """Send an XML request to the server and return the response"""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(("localhost", 12345))
            s.settimeout(10.0)
            
            # Send request
            xml_bytes = xml_str.encode('utf-8')
            s.sendall(struct.pack("!I", len(xml_bytes)) + xml_bytes)
            
            # Receive response length
            raw_len = s.recv(4)
            if len(raw_len) != 4:
                print(f"Error: Received {len(raw_len)} bytes for length header")
                return None
                
            msg_len = struct.unpack("!I", raw_len)[0]
            
            # Receive response content
            data = b''
            while len(data) < msg_len:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
            
            return data.decode()
    except Exception as e:
        print(f"Error: {e}")
        return None

def test_account_creation():
    """Test creating an account and verify it's in the database"""
    # Clear any existing test data
    clear_test_data()
    
    # Generate a unique test account ID
    account_id = f"test_account_{int(time.time())}"
    balance = 5000.00
    
    print(f"\n=== Testing Account Creation: {account_id} ===")
    
    # Create XML request
    xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<create>
    <account id="{account_id}" balance="{balance:.2f}"/>
</create>"""
    
    print("Sending request:")
    print(xml)
    
    # Send request
    response = send_request(xml)
    
    if not response:
        print("❌ No response received")
        return False
        
    print("\nReceived response:")
    print(response)
    
    # Parse response
    try:
        root = ET.fromstring(response)
        created = root.find("./created")
        if created is not None and created.get("id") == account_id:
            print(f"✅ Response contains created element for {account_id}")
        else:
            print("❌ Response missing created element")
    except Exception as e:
        print(f"Error parsing response: {e}")
    
    # Check if account exists in database
    print("\nChecking database...")
    time.sleep(1)  # Give the database a moment to update
    return check_account_in_db(account_id)

if __name__ == "__main__":
    result = test_account_creation()
    if result:
        print("\n✅ TEST PASSED: Account was created and found in database")
    else:
        print("\n❌ TEST FAILED: Account was not created or not found in database")