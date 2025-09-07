#!/bin/bash

# 当任何命令失败时，立即退出脚本
set -e

# 获取脚本所在的目录，也就是 external_libs 目录
BASE_DIR=$(pwd)
INSTALL_DIR="${BASE_DIR}/install"
echo "Dependencies will be installed to: ${INSTALL_DIR}"

# 创建安装目录
mkdir -p ${INSTALL_DIR}

# 1. 编译 Paho C
echo "Building Paho MQTT C..."
cd "${BASE_DIR}/paho.mqtt.c"
mkdir -p build && cd build
rm -rf * # 清理旧的 cmake 缓存
cmake .. -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
         -DPAHO_WITH_SSL=ON \
         -DPAHO_BUILD_STATIC=ON \
         -DPAHO_BUILD_SHARED=OFF
make -j$(nproc)
make install

# 2. 编译 yaml-cpp
echo "Building yaml-cpp..."
cd "${BASE_DIR}/yaml-cpp"
mkdir -p build && cd build
rm -rf *
cmake .. -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
         -DBUILD_SHARED_LIBS=OFF \
         -DYAML_CPP_BUILD_TESTS=OFF
make -j$(nproc)
make install

# 3. 编译 libusb (Autotools)
echo "Building libusb..."
cd "${BASE_DIR}/libusb"
if [ ! -f "configure" ]; then
    ./autogen.sh
fi
make distclean || true # 清理旧的 configure 结果
./configure --prefix=${INSTALL_DIR} --disable-shared --enable-static
make -j$(nproc)
make install

# 4. 编译 opus (CMake)
echo "Building opus..."
cd "${BASE_DIR}/opus"
mkdir -p build && cd build
rm -rf *
cmake .. -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} -DBUILD_SHARED_LIBS=OFF
make -j$(nproc)
make install

# 5. 编译 jsoncpp (CMake)
echo "Building jsoncpp..."
cd "${BASE_DIR}/jsoncpp"
mkdir -p build && cd build
rm -rf *
cmake .. -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} -DBUILD_SHARED_LIBS=OFF -DJSONCPP_WITH_TESTS=OFF
make -j$(nproc)
make install
    
# 6. 编译 libhv (CMake)
echo "Building libhv..."
cd "${BASE_DIR}/libhv"
mkdir -p build && cd build
rm -rf *
cmake .. -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} -DBUILD_SHARED_LIBS=OFF -DWITH_EXAMPLES=OFF
make -j$(nproc)
make install

echo "All dependencies built and installed successfully!"
