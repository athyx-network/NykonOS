#!/bin/bash
set -e

DRIVE="${1:-/dev/sdb}"

if [ ! -b "$DRIVE" ]; then
    echo "[Error] Block device '$DRIVE' not found!"
    echo "Usage: sudo ./flash_sd.sh /dev/sdb"
    exit 1
fi

echo "=========================================================="
echo "          Nykon OS Banana Pi M2 Zero SD Flasher           "
echo "=========================================================="
echo "Target Drive: $DRIVE"
echo ""
echo "WARNING: All data on $DRIVE will be replaced with Nykon OS!"
echo "Press Ctrl+C to cancel, or wait 3 seconds to continue..."
sleep 3

# Step 1: Ensure latest Banana Pi M2 Zero build
echo "[1/4] Building latest Nykon OS for Banana Pi M2 Zero..."
export PYTHONPATH="/home/bot/.local/lib/python3.14/site-packages:$PYTHONPATH"
./make.sh bpi

# Step 2: Unmount any active mounts on the drive
echo "[2/4] Unmounting any active partitions on $DRIVE..."
umount ${DRIVE}* 2>/dev/null || true

# Step 3: Create partition and write U-Boot bootloader
echo "[3/4] Partitioning $DRIVE..."
parted -s "$DRIVE" mklabel msdos
parted -s "$DRIVE" mkpart primary fat32 2048s 100%
parted -s "$DRIVE" set 1 boot on

echo "Writing Banana Pi M2 Zero Allwinner U-Boot bootloader..."
if [ -f "build_tools/u-boot-sunxi-with-spl.bin" ]; then
    dd if=build_tools/u-boot-sunxi-with-spl.bin of="$DRIVE" bs=1k seek=8 conv=notrunc status=none
fi
sync
sleep 1

# Detect partition name (e.g. /dev/sdb1 or /dev/mmcblk0p1)
if [[ "$DRIVE" == *"mmcblk"* ]] || [[ "$DRIVE" == *"nvme"* ]]; then
    PART="${DRIVE}p1"
else
    PART="${DRIVE}1"
fi

echo "Formatting $PART as FAT32..."
mkfs.vfat -F 32 -n "NYKON_OS" "$PART"

# Step 4: Copy Nykon OS files to SD Card
echo "[4/4] Copying Nykon OS files..."
MOUNT_DIR="/tmp/nykon_sd_mount"
mkdir -p "$MOUNT_DIR"
mount "$PART" "$MOUNT_DIR"

cp build_bpi/os.bin "$MOUNT_DIR/os.bin"
cp build_bpi/rootfs.tar "$MOUNT_DIR/rootfs.tar"
cp build_bpi/boot.cmd "$MOUNT_DIR/boot.cmd"
if [ -f build_bpi/boot.scr ]; then
    cp build_bpi/boot.scr "$MOUNT_DIR/boot.scr"
fi

sync
umount "$MOUNT_DIR"
rm -rf "$MOUNT_DIR"

if [ -n "$SUDO_USER" ]; then
    chown -R "$SUDO_USER":"$SUDO_USER" build_bpi build 2>/dev/null || true
fi

echo ""
echo "=========================================================="
echo " SUCCESS: Nykon OS has been flashed to $DRIVE!"
echo "=========================================================="
echo "1. Safely remove the MicroSD card."
echo "2. Insert it into your Banana Pi M2 Zero."
echo "3. Connect Mini-HDMI to your monitor and power on (5V)."
echo "=========================================================="
