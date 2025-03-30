FROM gcc:latest

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    gdb \
    libtinyxml2-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . /app

RUN g++ -g -O0 -fsanitize=address -fno-omit-frame-pointer \
-std=c++17 -Wall -Wextra -pthread \
src/*.cpp -ltinyxml2 -o trading_server_asan





EXPOSE 12345

CMD ["tail","-f","/dev/null"]


