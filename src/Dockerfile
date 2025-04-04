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

RUN echo "#!/bin/bash" > /app/check-port.sh && \
    echo "nc -z -v -w5 0.0.0.0 12345 || echo 'Port 12345 is available'" >> /app/check-port.sh && \
    chmod +x /app/check-port.sh

RUN mkdir -p build
WORKDIR /app/build
RUN cmake .. && make

EXPOSE 12345

# CMD ["/bin/bash", "-c", "/app/check-port.sh && ./exchange_server"]
CMD ["./exchange_server"]

