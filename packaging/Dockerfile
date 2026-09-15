FROM debian:12-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3=3.11.2-1+b1 python3-venv=3.11.2-1+b1 python3-pip=23.0.1+dfsg-1 \
    python3-rdkit=202209.3-1 python3-numpy=1:1.24.2-1+deb12u1 \
    openjdk-17-jdk-headless=17.0.20.1+1-1~deb12u1 ca-certificates=20250419~deb12u1 \
    binutils=2.40-2 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work/source
COPY . .
RUN ./packaging/build-runtime.sh /work/payload /work/package-build \
    && tar -C /work -czf /work/iupac-chemistry-linux-arm64.tar.gz payload \
    && sha256sum /work/iupac-chemistry-linux-arm64.tar.gz > /work/iupac-chemistry-linux-arm64.tar.gz.sha256 \
    && wc -c /work/iupac-chemistry-linux-arm64.tar.gz > /work/iupac-chemistry-linux-arm64.tar.gz.bytes

FROM debian:12-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171 AS runtime
COPY --from=build /work/payload /opt/iupac-chemistry
COPY packaging/clean-runtime-test.sh /usr/local/bin/clean-runtime-test
ENTRYPOINT ["/usr/local/bin/clean-runtime-test"]

FROM scratch AS export
COPY --from=build /work/iupac-chemistry-linux-arm64.tar.gz /
COPY --from=build /work/iupac-chemistry-linux-arm64.tar.gz.sha256 /
COPY --from=build /work/iupac-chemistry-linux-arm64.tar.gz.bytes /
COPY --from=build /work/payload/package-report.json /
