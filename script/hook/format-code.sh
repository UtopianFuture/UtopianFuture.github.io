#!/bin/bash

# https://ortogonal.github.io/cpp/git-clang-format/

# Test clang-format
clangformatout=$(git clang-format --diff -q)

# Redirect output to stderr.
exec 1>&2

if [ "$clangformatout" == "no modified files to format" ]; then
    exit 0
fi

if [ "$clangformatout" != "" ]; then
    echo "---------------------------------------------------------------------"
    echo "$clangformatout"
    echo "---------------------------------------------------------------------"
    exit 1
fi
