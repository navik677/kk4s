# Reproducible Switch build environment for kk4s.
#   docker build -t kk4s-build .
#   docker run --rm -v "$PWD":/src kk4s-build bash tools/build-switch.sh
# Result: out/kk4s.nro and out/kk4s.elf
FROM devkitpro/devkita64:latest

# Pre-install the portlibs once; later builds skip this step (SKIP_DEPS=1).
COPY tools/build-switch.sh /tmp/build-switch.sh
RUN DEPS_ONLY=1 bash /tmp/build-switch.sh && rm -f /tmp/build-switch.sh

ENV SKIP_DEPS=1
WORKDIR /src
