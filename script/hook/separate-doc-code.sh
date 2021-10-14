#!/bin/bash

mapfile -t files < <(git diff --name-only --cached)
has_code=false
has_doc=false

for fullfile in "${files[@]}"; do
    # or do whatever with individual element of the array
    filename=$(basename -- "$fullfile")
    extension="${filename##*.}"
    if [ "$extension" == "c" ] || [ "$extension" == "h" ]; then
        has_code=true
    elif [ "$extension" == "md" ]; then
        has_doc=true
    fi
done

if [ "$has_code" == true ] && [ $has_doc == "true" ]; then
    exit 1
fi
