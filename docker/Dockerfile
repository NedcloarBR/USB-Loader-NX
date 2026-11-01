# Atmosphère's build image (built from atmosphere/Dockerfile) plus a newer libnx: the pinned Atmosphère needs
# FsStorageForBatchRead, which the published libnx lacks.
ARG BASE_IMAGE=atms-build
FROM ${BASE_IMAGE}

USER root
ARG LIBNX_REV=b156b4774fb2df7b93402169509266da7547fc4c
RUN mkdir -p /tmp/libnx && cd /tmp/libnx && \
    git init -q && git remote add origin https://github.com/switchbrew/libnx.git && \
    git fetch -q --depth 1 origin $LIBNX_REV && git reset -q --hard FETCH_HEAD && \
    make -j"$(nproc)" -C nx && make -C nx install && rm -rf /tmp/libnx
USER atmosphere
