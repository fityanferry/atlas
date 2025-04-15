#!/bin/bash

# Export variables for compiling
export ARCH=arm64
export LLVM=1
export LLVM_IAS=1

# Enter build directory
mkdir out

# Compile kernel
rm -rf out/
make clean
make mrproper
make ARCH=arm64 O=out CC=clang exynos850-a21snsxx_defconfig
make ARCH=arm64 O=out CC=clang -j$(nproc --all)
