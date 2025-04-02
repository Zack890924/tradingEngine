FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    make \
    libpq-dev \
    libpqxx-dev \
    netcat \
    iproute2 \
    lsof \
    iputils-ping \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app/

RUN mkdir -p build && cd build && \
    cmake .. && \
    make

# Add a check to verify the server will be able to bind to the port
RUN echo "#!/bin/bash" > /app/check-port.sh && \
    echo "nc -z -v -w5 0.0.0.0 12345 || echo 'Port 12345 is available'" >> /app/check-port.sh && \
    chmod +x /app/check-port.sh

EXPOSE 12345

# Run the check before starting the server
CMD ["/bin/bash", "-c", "/app/check-port.sh && ./build/exchange_server"]


