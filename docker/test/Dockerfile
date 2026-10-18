# Desktop integration-test environment: the official Ren'Py Linux SDK under
# Xvfb with Mesa's software OpenGL (llvmpipe). Not a Switch environment.
# The X11 extension libraries are required: without them the SDK's SDL calls
# a null function pointer while pumping events.
FROM debian:bookworm-20260918-slim@sha256:3783cc01769c7b2b1b83a5c5ad96c815348e28ed7da68e2e3687004faa906251
RUN apt-get update \
 && apt-get install -y --no-install-recommends xvfb xauth libgl1 libegl1 libgles2 libgl1-mesa-dri bzip2 \
      libxcursor1 libxi6 libxrandr2 libxss1 libxfixes3 libxext6 libxinerama1 libxxf86vm1 libxkbcommon0 \
 && rm -rf /var/lib/apt/lists/*
