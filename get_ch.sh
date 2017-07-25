#!/bin/sh

#sudo apt-get install software-properties-common
#sudo apt-add-repository ppa:ubuntu-toolchain-r/test
#sudo apt-get update
#sudo apt-get install git cmake gcc-6 g++-6 libicu-dev libreadline-dev libmysqlclient-dev libssl-dev unixodbc-dev clang -y
git clone -b stable https://github.com/yandex/ClickHouse.git
cd ClickHouse
mkdir build
cd build
cmake .. -DUSE_STATIC_LIBRARIES=0
make clickhouse -j 4
