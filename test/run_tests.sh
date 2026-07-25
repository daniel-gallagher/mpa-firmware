#!/bin/sh
# Host-side unit tests for the firmware's report-building logic.
#
# The .ino is plain C++, so with a handful of stubs (a simulated TCA9555 that
# honours the polarity registers, a fake millis(), a capture-only usb_mpa) the
# whole state machine runs on a PC. No Teensy required.
set -e
cd "$(dirname "$0")"
cp ../core-patch/teensy4/usb_mpa.h stub/
g++ -std=gnu++17 -O1 -Wall -Wextra -Wno-unused-parameter \
    -I stub -I ../firmware/teensympa \
    test_main.cpp stub/usb_mpa_stub.cpp -o /tmp/teensympa_test
exec /tmp/teensympa_test
