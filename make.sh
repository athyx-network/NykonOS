#!/bin/bash
set -e

TARGET="${1:-qemu}"
export PYTHONPATH="/home/bot/.local/lib/python3.14/site-packages:$PYTHONPATH"

cd src

if [ "$TARGET" == "bpi" ]; then
    echo "========================================="
    echo "Building Nykon OS for Banana Pi M2 Zero"
    echo "(Allwinner H2+/H3 Cortex-A7 Bare-Metal)"
    echo "========================================="
    
    mkdir -p ../build_bpi
    rm -rf ../build_bpi/*.o ../build_bpi/os.*
    
    echo "Registering apps..."
    python3 ../build_tools/register_apps.py
    
    echo "Compiling for Cortex-A7..."
    CPU_FLAGS="-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -DTARGET_BPI"
    
    arm-none-eabi-gcc -c -mcpu=cortex-a7 -DTARGET_BPI -x assembler-with-cpp sys/boot/startup.s -o ../build_bpi/startup.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/main.c -o ../build_bpi/main.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/fb.c -o ../build_bpi/fb.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/mouse.c -o ../build_bpi/mouse.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/keyboard.c -o ../build_bpi/keyboard.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/mm.c -o ../build_bpi/mm.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/fs.c -o ../build_bpi/fs.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/fs/block_dev.c -o ../build_bpi/block_dev.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/fs/fat32.c -o ../build_bpi/fat32.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/api.c -o ../build_bpi/api.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/app_registry.c -o ../build_bpi/app_registry.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/audio.c -o ../build_bpi/audio.o

    # Compile all apps
    find apps -name "*.c" | while read -r app; do
        if [ -f "$app" ]; then
            filename=$(basename -- "$app")
            app_name="${filename%.*}"
            arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g "$app" -o "../build_bpi/${app_name}.o"
        fi
    done

    if [ -d "../test" ]; then
        find ../test -name "*.c" | while read -r app; do
            if [ -f "$app" ]; then
                filename=$(basename -- "$app")
                app_name="${filename%.*}"
                arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g "$app" -o "../build_bpi/${app_name}.o"
            fi
        done
    fi

    echo "Linking kernel..."
    arm-none-eabi-ld -T sys/linker_bpi.ld ../build_bpi/*.o -o ../build_bpi/os.elf
    arm-none-eabi-objcopy -O binary ../build_bpi/os.elf ../build_bpi/os.bin

    echo "Building filesystem..."
    rm -rf ../build_bpi/rootfs_staging
    mkdir -p ../build_bpi/rootfs_staging
    cp -r * ../build_bpi/rootfs_staging/
    python3 ../build_tools/img_compiler.py ../build_bpi/rootfs_staging
    (cd ../build_bpi/rootfs_staging && tar -cf ../rootfs.tar *)

    # Create U-Boot boot script
    cat << 'EOF' > ../build_bpi/boot.cmd
setenv video-mode sunxi:1280x720-24@60,monitor=hdmi,hpd=0,edid=0
load mmc 0:1 0x42000000 os.bin
load mmc 0:1 0x46000000 rootfs.tar
go 0x42000000
EOF

    python3 ../build_tools/make_boot_scr.py ../build_bpi/boot.cmd ../build_bpi/boot.scr

    echo "Generating bootable SD card disk image for Balena Etcher..."
    IMG_PATH="../build_bpi/nykon-bpi-m2-zero.img"
    PART_TMP="/tmp/nykon_fat_$$.img"
    
    # 1. Create 64MB disk image
    dd if=/dev/zero of="$IMG_PATH" bs=1M count=64 status=none
    
    # 2. Write U-Boot SPL at 8KB offset (sector 16)
    dd if=../build_tools/u-boot-sunxi-with-spl.bin of="$IMG_PATH" bs=1k seek=8 conv=notrunc status=none
    
    # 3. Create partition table with sfdisk (Partition 1 starts at 2048 sectors = 1MB)
    printf "2048,,c,*\n" | sfdisk -q "$IMG_PATH" > /dev/null 2>&1
    
    # 4. Create 63MB FAT32 partition and copy boot assets
    mkfs.vfat -F 32 -n "NYKON_OS" -C "$PART_TMP" $((63 * 1024)) > /dev/null 2>&1
    mcopy -i "$PART_TMP" ../build_bpi/os.bin ../build_bpi/rootfs.tar ../build_bpi/boot.scr ../build_bpi/boot.cmd ::/
    
    # 5. Burn partition into image at 1MB offset
    dd if="$PART_TMP" of="$IMG_PATH" bs=1M seek=1 conv=notrunc status=none
    rm -f "$PART_TMP"

    echo ""
    echo "========================================================="
    echo "SUCCESS: Banana Pi M2 Zero image created!"
    echo "  -> build_bpi/nykon-bpi-m2-zero.img (64MB Bootable Disk Image)"
    echo "========================================================="
    echo "How to Flash with Balena Etcher:"
    echo "1. Open Balena Etcher (or Raspberry Pi Imager)."
    echo "2. Select: 'Flash from file' -> choose 'build_bpi/nykon-bpi-m2-zero.img'"
    echo "3. Select your SD Card target."
    echo "4. Click 'Flash!' and insert into your Banana Pi M2 Zero."
    echo "========================================================="

elif [ "$TARGET" == "tab" ] || [ "$TARGET" == "msm8916" ] || [ "$TARGET" == "t560nu" ]; then
    echo "========================================="
    echo "Building Nykon OS for Samsung Galaxy Tab E"
    echo "(SM-T560NU / Qualcomm Snapdragon 410 / lk2nd)"
    echo "========================================="
    
    mkdir -p ../build_tab
    rm -rf ../build_tab/*.o ../build_tab/os.*
    
    echo "Registering apps..."
    python3 ../build_tools/register_apps.py
    
    echo "Compiling for Cortex-A53 / MSM8916..."
    CPU_FLAGS="-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -DTARGET_MSM8916"
    
    arm-none-eabi-gcc -c -mcpu=cortex-a7 -DTARGET_MSM8916 -x assembler-with-cpp sys/boot/startup.s -o ../build_tab/startup.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/main.c -o ../build_tab/main.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/fb.c -o ../build_tab/fb.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/mouse.c -o ../build_tab/mouse.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/keyboard.c -o ../build_tab/keyboard.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/mm.c -o ../build_tab/mm.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/fs.c -o ../build_tab/fs.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/fs/block_dev.c -o ../build_tab/block_dev.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/fs/fat32.c -o ../build_tab/fat32.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/api.c -o ../build_tab/api.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/app_registry.c -o ../build_tab/app_registry.o
    arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g sys/kernel/audio.c -o ../build_tab/audio.o

    # Compile all apps
    find apps -name "*.c" | while read -r app; do
        if [ -f "$app" ]; then
            filename=$(basename -- "$app")
            app_name="${filename%.*}"
            arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g "$app" -o "../build_tab/${app_name}.o"
        fi
    done

    if [ -d "../test" ]; then
        find ../test -name "*.c" | while read -r app; do
            if [ -f "$app" ]; then
                filename=$(basename -- "$app")
                app_name="${filename%.*}"
                arm-none-eabi-gcc -c $CPU_FLAGS -O3 -fno-builtin -g "$app" -o "../build_tab/${app_name}.o"
            fi
        done
    fi

    echo "Linking kernel for MSM8916..."
    arm-none-eabi-ld -T sys/linker_msm8916.ld ../build_tab/*.o -o ../build_tab/os.elf
    arm-none-eabi-objcopy -O binary ../build_tab/os.elf ../build_tab/os.bin

    echo "Building filesystem..."
    rm -rf ../build_tab/rootfs_staging
    mkdir -p ../build_tab/rootfs_staging
    cp -r * ../build_tab/rootfs_staging/
    python3 ../build_tools/img_compiler.py ../build_tab/rootfs_staging
    (cd ../build_tab/rootfs_staging && tar -cf ../rootfs.tar *)

    echo "Generating bootable SD image for Samsung Galaxy Tab E (lk2nd)..."
    IMG_PATH="../build_tab/nykon-tab-e-sd.img"
    PART_TMP="/tmp/nykon_tab_fat_$$.img"
    
    # 1. Create 64MB disk image
    dd if=/dev/zero of="$IMG_PATH" bs=1M count=64 status=none
    
    # 2. Partition with sfdisk (FAT32 starting at 1MB offset)
    printf "2048,,c,*\n" | sfdisk -q "$IMG_PATH" > /dev/null 2>&1
    
    # 3. Create 63MB FAT32 partition and copy boot assets
    mkfs.vfat -F 32 -n "NYKON_TAB" -C "$PART_TMP" $((63 * 1024)) > /dev/null 2>&1
    mcopy -i "$PART_TMP" ../build_tab/os.bin ../build_tab/rootfs.tar ::/
    
    # 4. Burn partition into image at 1MB offset
    dd if="$PART_TMP" of="$IMG_PATH" bs=1M seek=1 conv=notrunc status=none
    rm -f "$PART_TMP"

    echo ""
    echo "========================================================="
    echo "SUCCESS: Samsung Galaxy Tab E (SM-T560NU) image created!"
    echo "  -> build_tab/nykon-tab-e-sd.img (SD Card Disk Image)"
    echo "  -> build_tab/os.bin            (Snapdragon 410 Payload)"
    echo "  -> build_tab/rootfs.tar        (Filesystem Archive)"
    echo "========================================================="
    echo "Dual-Boot with LineageOS using lk2nd:"
    echo "1. Flash lk2nd (samsung-gtelwifi) to your tablet."
    echo "2. Flash 'build_tab/nykon-tab-e-sd.img' to a MicroSD card with Balena Etcher."
    echo "3. Insert MicroSD card into your Tab E."
    echo "4. Turn on holding Vol Down -> lk2nd boot menu will let you pick:"
    echo "   - Boot LineageOS (eMMC internal memory)"
    echo "   - Boot Nykon OS (MicroSD card)"
    echo "========================================================="

else
    mkdir -p ../build

    echo "Registering apps..."
    python3 ../build_tools/register_apps.py

    echo "Compiling for QEMU VersatilePB..."
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -x assembler-with-cpp sys/boot/startup.s -o ../build/startup.o
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/main.c -o ../build/main.o
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/fb.c -o ../build/fb.o
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/mouse.c -o ../build/mouse.o
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/keyboard.c -o ../build/keyboard.o
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/mm.c -o ../build/mm.o
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/fs.c -o ../build/fs.o
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/fs/block_dev.c -o ../build/block_dev.o
    arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g sys/kernel/fs/fat32.c -o ../build/fat32.o
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

    if [ -d "../test" ]; then
        find ../test -name "*.c" | while read -r app; do
            if [ -f "$app" ]; then
                filename=$(basename -- "$app")
                app_name="${filename%.*}"
                arm-none-eabi-gcc -c -mcpu=arm926ej-s -O3 -fno-builtin -g "$app" -o "../build/${app_name}.o"
            fi
        done
    fi

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
fi
