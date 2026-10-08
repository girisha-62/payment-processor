FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake make ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /app/build/payment_processor .
ENTRYPOINT ["./payment_processor"]
CMD ["4", "6", "20"]
