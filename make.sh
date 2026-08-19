#!/bin/bash
set -e
cd src
mkdir -p ../build

echo "Registering apps..."
python3 ../build_tools/register_apps.py

echo "Compiling..."
arm-none-eabi-as -mcpu=arm926ej-s -g sys/boot/startup.s -o ../build/startup.o
arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/main.c -o ../build/main.o
arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/fb.c -o ../build/fb.o
arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/mouse.c -o ../build/mouse.o
arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/keyboard.c -o ../build/keyboard.o
arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/fs.c -o ../build/fs.o
arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/api.c -o ../build/api.o
arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/app_registry.c -o ../build/app_registry.o
arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/audio.c -o ../build/audio.o

# Compile all apps
find apps -name "*.c" | while read -r app; do
    if [ -f "$app" ]; then
        filename=$(basename -- "$app")
        app_name="${filename%.*}"
        arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g "$app" -o "../build/${app_name}.o"
    fi
done

arm-none-eabi-ld -T sys/linker.ld ../build/*.o -o ../build/os.elf
arm-none-eabi-objcopy -O binary ../build/os.elf ../build/os.bin

echo "Building filesystem..."
rm -rf ../build/rootfs_staging
mkdir -p ../build/rootfs_staging
cp -r * ../build/rootfs_staging/
python3 ../build_tools/img_compiler.py ../build/rootfs_staging
(cd ../build/rootfs_staging && tar -cf ../rootfs.tar *)

echo "Running in QEMU..."
echo "NOTE: A graphical QEMU window will open."
echo "To see the 'Hello World!' output, you must switch to the serial console within the window."
echo "Press Ctrl+Alt+3 to switch to serial0."

qemu-system-arm -M versatilepb -m 256M -kernel ../build/os.bin -serial vc -rtc base=localtime -device loader,file=../build/rootfs.tar,addr=0x01000000
