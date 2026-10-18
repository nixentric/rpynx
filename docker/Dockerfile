# Build environment: the pinned devkitA64 image plus host libraries needed to
# build the host CPython that the Switch cross build requires.
FROM devkitpro/devkita64:20260219@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
RUN apt-get update \
 && apt-get install -y --no-install-recommends zlib1g-dev libbz2-dev liblzma-dev libffi-dev libssl-dev \
 && rm -rf /var/lib/apt/lists/*
