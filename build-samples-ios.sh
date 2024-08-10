#!/bin/bash

set -e

echo "Downloading binary dependencies for samples..."
curl https://github.com/nicebyte/nicegraf/releases/download/v0.1.1/nicegraf-samples-data.zip -fL -o nicegraf-samples-data.zip
echo "Unpacking binary dependencies and data for samples..."
unzip -u nicegraf-samples-data.zip
chmod +x ./samples/deps/niceshade/macos/niceshade
chmod +x ./samples/deps/niceshade/linux/niceshade
echo "Removing temporary files..."
rm -rf nicegraf-samples-data.zip
echo "Downloading library dependencies for samples..."
git submodule init
git submodule update
echo "Setting up folder for build files..."
mkdir -p samples-build-files
cd samples-build-files
echo "Generating build files..."

cmake .. -DNGF_BUILD_SAMPLES="yes" -DCMAKE_SYSTEM_NAME="iOS" -GXcode $@
cd ..
echo "Finished successfully!"
