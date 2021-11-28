@echo off

echo "Downloading binary dependencies for samples"
git lfs install || (exit /b)
git lfs pull --exclude="" || (exit /b)
echo "Downloading library dependencies for samples"
git submodule init || (exit /b)
git submodule update || (exit /b)
if not exist ".\samples-build-files" mkdir samples-build-files || (exit /b)
cd samples-build-files || (exit /b)
cmake .. -DNGF_BUILD_SAMPLES="yes" || (exit /b)
pause
