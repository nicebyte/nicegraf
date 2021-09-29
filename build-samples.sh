#!/usr/bin/bash

set -e

cp ./samples/install-submodules ./.gitmodules
git submodule init
git submodule update
mkdir -p samples-build-files
cd samples-build-files
cmake .. -DNGF_BUILD_SAMPLES="yes"
cd ..
