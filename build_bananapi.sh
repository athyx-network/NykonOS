#!/bin/bash
echo "Building Nykon OS for Banana Pi (Linux Framebuffer Mode)..."

# Create build directory
mkdir -p build_linux
mkdir -p build

echo "Building filesystem..."
rm -rf build/rootfs_staging
mkdir -p build/rootfs_staging
cp -r src/* build/rootfs_staging/
python3 build_tools/img_compiler.py build/rootfs_staging
(cd build/rootfs_staging && tar -cf ../rootfs.tar *)

echo "Registering apps..."
python3 build_tools/register_apps.py

echo "Cleaning old object files..."
rm -f build/*.o

echo "Compiling..."
gcc -c src/sys/kernel/main.c -o build/main.o -D LINUX_BUILD
gcc -c src/sys/kernel/fb.c -o build/fb.o -D LINUX_BUILD
gcc -c src/sys/kernel/mouse.c -o build/mouse.o -D LINUX_BUILD
gcc -c src/sys/kernel/keyboard.c -o build/keyboard.o -D LINUX_BUILD
gcc -c src/sys/kernel/fs.c -o build/fs.o -D LINUX_BUILD
gcc -c src/sys/kernel/api.c -o build/api.o -D LINUX_BUILD
gcc -c src/sys/kernel/app_registry.c -o build/app_registry.o -D LINUX_BUILD
gcc -c src/sys/kernel/audio.c -o build/audio.o -D LINUX_BUILD

for app in src/apps/*.c; do
    if [ -f "$app" ]; then
        filename=$(basename -- "$app")
        app_name="${filename%.*}"
        gcc -c "$app" -o "build/${app_name}.o" -D LINUX_BUILD
    fi
done

gcc build/*.o -o build_linux/nykon_os

if [ $? -eq 0 ]; then
    echo "Success! The executable is located at: build_linux/nykon_os"
    echo "To run this on your Banana Pi:"
    echo "1. Switch to a pure TTY terminal (Ctrl+Alt+F1)"
    echo "2. Run: sudo ./build_linux/nykon_os"
    echo "Note: It must be run with sudo to access /dev/fb0 and /dev/input"
else
    echo "Compilation failed."
fi
