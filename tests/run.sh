#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")/.."
./tests/resources.sh
flatpak build build sh -c 'c++ -std=c++20 -fPIC tests/udisks2.cpp -o build/udisks2-test $(pkg-config --cflags --libs Qt6Core Qt6DBus)'
dbus-run-session -- /usr/bin/python3 tests/mock-udisks2.py sh -c '
    exec flatpak build --filesystem=/tmp \
        --env=DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SYSTEM_BUS_ADDRESS" \
        --env=RPI_IMAGER_MOCK_UDISKS=1 build "$PWD/build/udisks2-test"
'
