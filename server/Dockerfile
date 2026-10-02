# Этап 1: сборка (Qt6 + mongocxx + argon2). Пакеты под Ubuntu 24.04.
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config curl ca-certificates \
      qt6-base-dev qt6-httpserver-dev qt6-websockets-dev \
      libssl-dev libargon2-dev \
    && rm -rf /var/lib/apt/lists/*

# mongo-c-driver (C) — shared, без SASL/snappy/zstd чтобы не тащить лишнее в рантайм.
# SCRAM-SHA-256 для root-авторизации работает и без cyrus-sasl.
ARG MONGO_C_VERSION=1.30.5
RUN curl -fsSL -o /tmp/mongo-c.tar.gz \
      https://github.com/mongodb/mongo-c-driver/releases/download/${MONGO_C_VERSION}/mongo-c-driver-${MONGO_C_VERSION}.tar.gz \
 && tar -xzf /tmp/mongo-c.tar.gz -C /tmp \
 && cmake -S /tmp/mongo-c-driver-${MONGO_C_VERSION} -B /tmp/mongo-c-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_TESTS=OFF -DENABLE_EXAMPLES=OFF \
      -DENABLE_SASL=OFF -DENABLE_SNAPPY=OFF -DENABLE_ZSTD=OFF \
 && cmake --build /tmp/mongo-c-build && cmake --install /tmp/mongo-c-build \
 && rm -rf /tmp/mongo-c.tar.gz /tmp/mongo-c-driver-${MONGO_C_VERSION} /tmp/mongo-c-build

# mongo-cxx-driver (C++) 3.10, shared.
ARG MONGO_CXX_VERSION=3.10.2
RUN curl -fsSL -o /tmp/mongo-cxx.tar.gz \
      https://github.com/mongodb/mongo-cxx-driver/releases/download/r${MONGO_CXX_VERSION}/mongo-cxx-driver-r${MONGO_CXX_VERSION}.tar.gz \
 && tar -xzf /tmp/mongo-cxx.tar.gz -C /tmp \
 && cmake -S /tmp/mongo-cxx-driver-r${MONGO_CXX_VERSION} -B /tmp/mongo-cxx-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
      -DBUILD_TESTING=OFF -DENABLE_EXAMPLES=OFF \
 && cmake --build /tmp/mongo-cxx-build && cmake --install /tmp/mongo-cxx-build \
 && rm -rf /tmp/mongo-cxx.tar.gz /tmp/mongo-cxx-driver-r${MONGO_CXX_VERSION} /tmp/mongo-cxx-build \
 && ldconfig

COPY . /src
RUN cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /build

# Этап 2: рантайм без тулчейна.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      libqt6core6 libqt6network6 libqt6httpserver6 libqt6websockets6 \
      libssl3t64 libargon2-1 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /usr/local/lib/libmongoc-*.so* /usr/local/lib/
COPY --from=build /usr/local/lib/libbson-*.so* /usr/local/lib/
COPY --from=build /usr/local/lib/libmongocxx.so* /usr/local/lib/
COPY --from=build /usr/local/lib/libbsoncxx.so* /usr/local/lib/
RUN ldconfig
COPY --from=build /build/kisa-server /app/kisa-server
EXPOSE 8080
CMD ["/app/kisa-server"]
