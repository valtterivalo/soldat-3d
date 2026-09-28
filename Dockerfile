FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake libzstd-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /opt/soldat
COPY client3d/ client3d/
COPY shared/ shared/
RUN cmake -S client3d -B build -DCMAKE_BUILD_TYPE=Release -DSOLDAT3D_RENDERER=OFF -DBUILD_TESTING=OFF && cmake --build build --parallel --target soldat3d-server

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libzstd1 && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/soldat/build/soldat3d-server /usr/local/bin/
COPY --from=build /opt/soldat/client3d/assets/ /opt/soldat/client3d/assets/
COPY LICENSE.md /opt/soldat/LICENSE.md
USER 65532:65532
EXPOSE 23073/udp
ENTRYPOINT ["/usr/local/bin/soldat3d-server"]
CMD ["--map", "Arena2", "--bots", "6"]
