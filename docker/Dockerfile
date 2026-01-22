############################################################
# docker/Dockerfile.GreedySweep ── Rocky Linux 8 + GCC 13 (C++20)
#   CIM ATPG Tool - Dual Mode (Generate + Simulator)
############################################################
FROM rockylinux:8

# 1. 安裝依賴
RUN dnf -y install epel-release dnf-plugins-core && \
    dnf config-manager --set-enabled powertools && \
    dnf -y install gcc-toolset-13 json-devel && \
    dnf clean all && rm -rf /var/cache/dnf

# 2. 建立工作目錄
WORKDIR /app

# 3. 複製原始碼
COPY include/ ./include/
COPY src/main.cpp src/GreedySweepRunner.cpp src/FaultSimulationEvent.cpp ./src/

# 4. 編譯並清理
RUN . /opt/rh/gcc-toolset-13/enable && \
    g++ -std=c++20 -O2 -static-libstdc++ -static-libgcc \
        -I./include \
        src/main.cpp src/GreedySweepRunner.cpp src/FaultSimulationEvent.cpp \
        -o cim-atpg && \
    strip cim-atpg && \
    rm -rf include/ src/

# 5. 設定 PATH
ENV PATH="/app:${PATH}"

# 6. 設定 Entrypoint
ENTRYPOINT ["cim-atpg"]
CMD ["--help"]