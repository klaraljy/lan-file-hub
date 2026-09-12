# lan-file-hub 后端镜像（多阶段构建）。
# 说明：多阶段构建；运行阶段以非 root 用户运行，只带运行时依赖（阶段十五）。
# 镜像内同时包含 lan_file_hub_server 与 lan_file_hub_migrate（迁移由独立服务执行）。
# 构建方式：docker compose build backend

# ---- 构建阶段 ----
FROM ubuntu:24.04 AS builder

# 构建期可选代理：部分环境下 Docker 容器解析不了外网域名（vcpkg 下载会报
# "Couldn't resolve host name"），此时用宿主代理构建：
#   docker compose build --build-arg HTTPS_PROXY=http://host.docker.internal:65532 backend
# 不传这些参数时不影响正常构建（空值等价于不使用代理）。
ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG ALL_PROXY
ENV HTTP_PROXY=${HTTP_PROXY} HTTPS_PROXY=${HTTPS_PROXY} ALL_PROXY=${ALL_PROXY}

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build curl zip unzip tar git pkg-config \
        perl bison flex \
        ca-certificates python3 \
    && rm -rf /var/lib/apt/lists/*

# vcpkg：先复制 manifest，让依赖层可以被缓存
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
COPY vcpkg.json ./
RUN "$VCPKG_ROOT/vcpkg" install --triplet x64-linux --clean-after-build

COPY CMakeLists.txt CMakePresets.json ./
COPY backend ./backend
RUN cmake -S . -B build/release -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET=x64-linux \
        -DLAN_FILE_HUB_BUILD_TESTS=OFF \
    && cmake --build build/release --target lan_file_hub_server lan_file_hub_migrate \
    && strip build/release/bin/lan_file_hub_server build/release/bin/lan_file_hub_migrate

# ---- 运行阶段 ----
FROM ubuntu:24.04 AS runtime

# 构建期可选代理：部分环境下 Docker 容器解析不了外网域名（vcpkg 下载会报
# "Couldn't resolve host name"），此时用宿主代理构建：
#   docker compose build --build-arg HTTPS_PROXY=http://host.docker.internal:65532 backend
# 不传这些参数时不影响正常构建（空值等价于不使用代理）。
ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG ALL_PROXY
ENV HTTP_PROXY=${HTTP_PROXY} HTTPS_PROXY=${HTTPS_PROXY} ALL_PROXY=${ALL_PROXY}

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl libpq5 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --shell /usr/sbin/nologin --uid 10001 lanfilehub

WORKDIR /app
COPY --from=builder /src/build/release/bin/lan_file_hub_server /app/lan_file_hub_server
COPY --from=builder /src/build/release/bin/lan_file_hub_migrate /app/lan_file_hub_migrate
# 迁移脚本随镜像发布：生产环境由 migrate 服务执行，不由应用进程改 schema
COPY migrations /app/migrations

# 容器内以非 root 运行，对象存储目录挂在数据卷上
RUN mkdir -p /data/storage && chown -R lanfilehub:lanfilehub /app /data
USER lanfilehub

ENV APP_ENV=production \
    HTTP_PORT=8080 \
    STORAGE_ROOT=/data/storage

EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/health/live || exit 1

ENTRYPOINT ["/app/lan_file_hub_server"]
