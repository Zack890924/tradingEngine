import psycopg2
import sys
import os
import subprocess

def get_db_connection_info():
    """Get database connection info from docker-compose environment"""
    try:
        # Run docker-compose config to get the actual configuration
        output = subprocess.check_output(
            ["docker-compose", "config"], 
            text=True
        )
        
        # Parse basic configuration
        db_user = "exchange"  # Default username
        db_password = "exchange_pass"  # Default password
        db_name = "exchange_db"  # Default database name
        db_host = "postgres"  # Default hostname
        
        print(f"Using connection parameters:")
        print(f"  Host: {db_host}")
        print(f"  Database: {db_name}")
        print(f"  User: {db_user}")
        print(f"  Password: {'*' * len(db_password)}")
        
        return db_host, db_name, db_user, db_password
        
    except subprocess.CalledProcessError as e:
        print(f"Error getting docker-compose configuration: {e}")
        return None, None, None, None

def check_database():
    """Check the database structure and content"""
    db_host, db_name, db_user, db_password = get_db_connection_info()
    
    if not all([db_host, db_name, db_user, db_password]):
        print("Missing database connection information")
        return
    
    # Get the host IP for the database container
    try:
        output = subprocess.check_output(
            ["docker", "inspect", "-f", "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}", 
             "erss-hwk4-ar791-yc658_postgres_1"], 
            text=True
        ).strip()
        
        db_container_ip = output
        print(f"Database container IP: {db_container_ip}")
    except subprocess.CalledProcessError as e:
        print(f"Error getting database container IP: {e}")
        db_container_ip = db_host
        
    try:
        # Try connecting to the database both ways
        try:
            print(f"\nAttempting to connect to database at {db_host}...")
            conn = psycopg2.connect(
                host=db_host,
                database=db_name,
                user=db_user,
                password=db_password
            )
        except psycopg2.OperationalError:
            print(f"Could not connect to {db_host}, trying container IP {db_container_ip}...")
            conn = psycopg2.connect(
                host=db_container_ip,
                database=db_name,
                user=db_user,
                password=db_password
            )
            
        print("Successfully connected to the database!")
        
        # Create a cursor to execute queries
        cur = conn.cursor()
        
        # Get list of tables
        print("\nChecking tables in the database:")
        cur.execute("""
            SELECT table_name 
            FROM information_schema.tables 
            WHERE table_schema = 'public'
        """)
        tables = cur.fetchall()
        
        if not tables:
            print("No tables found in the database!")
            return
            
        print(f"Found {len(tables)} tables:")
        for table in tables:
            print(f"  - {table[0]}")
            
        # For each table, check its structure and content
        for table_name in [t[0] for t in tables]:
            print(f"\nTable: {table_name}")
            
            # Get column information
            cur.execute(f"""
                SELECT column_name, data_type 
                FROM information_schema.columns 
                WHERE table_name = '{table_name}'
            """)
            columns = cur.fetchall()
            print("Columns:")
            for col in columns:
                print(f"  - {col[0]} ({col[1]})")
            
            # Get row count
            cur.execute(f"SELECT COUNT(*) FROM {table_name}")
            count = cur.fetchone()[0]
            print(f"Row count: {count}")
            
            # If there's data, show a sample
            if count > 0:
                cur.execute(f"SELECT * FROM {table_name} LIMIT 5")
                rows = cur.fetchall()
                print("Sample data:")
                for row in rows:
                    print(f"  {row}")
            else:
                print("Table is empty - no data has been inserted!")
                
        # Close the connection
        cur.close()
        conn.close()
        
    except Exception as e:
        print(f"Error connecting to database: {e}")
        
if __name__ == "__main__":
    check_database()