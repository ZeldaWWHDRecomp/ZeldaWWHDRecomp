# wwhd-online-server in a ~5 MB image: a static musl build on Alpine, then FROM scratch.
# Build with server/docker-build.sh (it sends only the two folders the server needs), or from the
# repository root:  docker build -f server/Dockerfile -t wwhd-online-server .
FROM alpine:3.20 AS build
RUN apk add --no-cache g++ cmake ninja linux-headers
WORKDIR /src
COPY runtime/src/online/proto runtime/src/online/proto
COPY server server
RUN cmake -S server -B /b -G Ninja -DCMAKE_BUILD_TYPE=Release -DWWHD_ONLINE_STATIC=ON \
 && ninja -C /b wwhd-online-server && strip /b/wwhd-online-server

FROM scratch
COPY --from=build /b/wwhd-online-server /wwhd-online-server
WORKDIR /data
EXPOSE 7777/udp
ENTRYPOINT ["/wwhd-online-server"]
CMD ["--config", "/data/server.toml"]
