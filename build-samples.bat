@echo off

copy .\samples\install-submodules .gitmodules || (exit /b)
git submodule init || (exit /b)
git submodule update || (exit /b)
if not exist ".\samples-build-files" mkdir samples-build-files || (exit /b)
cd samples-build-files || (exit /b)
cmake .. -DNGF_BUILD_SAMPLES="yes" || (exit /b)
pause