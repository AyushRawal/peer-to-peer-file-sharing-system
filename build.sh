#!/bin/bash

set -xe

compileFlags="-Wall -Wpedantic -Wextra -Wconversion -Wshadow -Wsign-conversion -Wcast-align -pedantic -std=c++17 -lssl -lcrypto"

g++ -c common/utils.cpp -o utils
# shellcheck disable=SC2086
g++ $compileFlags utils tracker/tracker.cpp -o tracker.out
# shellcheck disable=SC2086
g++ $compileFlags utils client/client.cpp -o client.out
