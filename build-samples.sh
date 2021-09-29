#!/usr/bin/bash

set -e

cp ./samples/install-submodules ./.gitmodules
git submodule init
git submodule update
mkdir -p samples-build-files
cd samples-build-files

if  [ "`uname -s`" = "Darwin" ]; then
  NGF_GENERATOR="-GXcode" 
else
  NGF_GENERATOR=
fi

cmake .. -DNGF_BUILD_SAMPLES="yes" ${NGF_GENERATOR}
cd ..
