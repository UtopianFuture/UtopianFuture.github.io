#!/bin/bash

PROJECT_DIR=$(git rev-parse --show-toplevel)
cd "$PROJECT_DIR" || exit 1

if make -j10; then
    exit 0
fi
exit 1
