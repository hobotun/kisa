# Этап 1: сборка (Qt6 + HttpServer). Пакеты под Ubuntu 24.04.
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build \
      qt6-base-dev qt6-httpserver-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY . /src
RUN cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /build

# Этап 2: рантайм без тулчейна.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      libqt6core6 libqt6network6 libqt6httpserver6 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /build/kisa-server /app/kisa-server
EXPOSE 8080
CMD ["/app/kisa-server"]
