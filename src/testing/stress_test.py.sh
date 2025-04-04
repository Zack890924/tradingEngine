#!/bin/bash
set -x  # 啟用 debug 模式

IMAGE_NAME="erss-hwk4-ar791-yc658_exchange_server:latest"
NETWORK_NAME="erss-hwk4-ar791-yc658_default"
STRESS_PY="stress_test.py"
CORES=(1 2 4)
REQUESTS=100
ACCOUNT_XML="create_account.xml"

# 使用 Docker network 裡的 PostgreSQL
export DB_HOST=postgres
export DB_PORT=5432
export DB_USER=exchange
export DB_PASSWORD=exchange_password
export DB_NAME=exchange_db

echo "Starting scalability test using Docker run with core restrictions..."
echo "Output will be saved to results.csv"
echo "Cores,Throughput,AvgLatency" > results.csv

for CORE in "${CORES[@]}"; do
    echo "=== Testing with $CORE core(s) ==="

    # 停止所有使用 12345 的 container
    EXISTING=$(sudo docker ps -q --filter "publish=12345")
    if [ -n "$EXISTING" ]; then
        echo "Stopping existing containers using port 12345..."
        sudo docker stop $EXISTING
        sleep 2
    fi

    echo "Creating accounts..."
    sudo docker run --rm --network ${NETWORK_NAME} \
      --env DB_HOST=postgres \
      --env DB_PORT=5432 \
      --env DB_USER=exchange \
      --env DB_PASSWORD=exchange_password \
      --env DB_NAME=exchange_db \
      -v "$(pwd)/$ACCOUNT_XML":/app/create_account.xml \
      ${IMAGE_NAME} /app/test_client.py --xml create_account.xml >/dev/null 2>&1

    echo "-------------------- STARTING SERVER --------------------"
    CONTAINER_ID=$(sudo docker run --rm --network ${NETWORK_NAME} \
      --cpuset-cpus="0-$(($CORE-1))" -p 12345:12345 -d \
      --env DB_HOST=postgres \
      --env DB_PORT=5432 \
      --env DB_USER=exchange \
      --env DB_PASSWORD=exchange_password \
      --env DB_NAME=exchange_db \
      ${IMAGE_NAME})
    
    if [ -z "$CONTAINER_ID" ]; then
        echo "Failed to start container on $CORE core(s), skipping..."
        continue
    fi
    
    echo "Started container $CONTAINER_ID using $CORE core(s)"
    sleep 15  # 增加等待時間

    echo "Running stress test with $CORE thread(s)..."
    OUTPUT=$(python3 $STRESS_PY --num $REQUESTS --threads $CORE | tee /tmp/stress_log.txt)
    echo "$OUTPUT"
    
    THROUGHPUT=$(grep "Throughput" /tmp/stress_log.txt | awk '{print $3}')
    AVG_LATENCY=$(grep "Average Latency" /tmp/stress_log.txt | awk '{print $4}')
    
    echo "$CORE,$THROUGHPUT,$AVG_LATENCY" >> results.csv

    echo "Stopping container $CONTAINER_ID..."
    sudo docker stop $CONTAINER_ID
    sleep 2
done

echo ""
echo "✅ Scalability test completed. Results saved to results.csv"
