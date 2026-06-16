#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

TOOLCHAIN_DIR=$(find /usr/local/arm-cross-compiler/install /home -maxdepth 5 -name "${CROSS_COMPILE}gcc" -type f 2>/dev/null | head -1 | xargs dirname | xargs dirname)
export PATH=$PATH:${TOOLCHAIN_DIR}/bin
SYSROOT=${TOOLCHAIN_DIR}/aarch64-none-linux-gnu/libc

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
    make -j$(nproc) ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- Image
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p "$OUTDIR/rootfs"
mkdir -p $OUTDIR/rootfs/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,usr,var}
mkdir -p "$OUTDIR/rootfs/home/conf"

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone https://git.busybox.net/busybox
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make -j$(nproc) ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu-
make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- CONFIG_PREFIX=$OUTDIR/rootfs install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a $OUTDIR/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a $OUTDIR/rootfs/bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
sudo cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 $OUTDIR/rootfs/lib/
sudo cp ${SYSROOT}/lib64/libc.so.6 $OUTDIR/rootfs/lib64/
sudo cp ${SYSROOT}/lib64/libm.so.6 $OUTDIR/rootfs/lib64/
sudo cp ${SYSROOT}/lib64/libdl.so.2 $OUTDIR/rootfs/lib64/
sudo cp ${SYSROOT}/lib64/libpthread.so.0 $OUTDIR/rootfs/lib64/
sudo cp ${SYSROOT}/lib64/libresolv.so.2 $OUTDIR/rootfs/lib64/

# TODO: Make device nodes
sudo mknod -m 666 $OUTDIR/rootfs/dev/null c 1 3
sudo mknod -m 666 $OUTDIR/rootfs/dev/console c 5 1

# TODO: Clean and build the writer utility
cd $FINDER_APP_DIR
make clean
CROSS_COMPILE=aarch64-none-linux-gnu- make

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp $FINDER_APP_DIR/{finder.sh,writer.sh,writer,autorun-qemu.sh,finder-test.sh} $OUTDIR/rootfs/home/
cp -r $FINDER_APP_DIR/../conf/* $OUTDIR/rootfs/home/conf/

# TODO: Chown the root directory
sudo chown -R root:root $OUTDIR/rootfs

# TODO: Create initramfs.cpio.gz
cd $OUTDIR/rootfs
find . | cpio -H newc -o | gzip > $OUTDIR/initramfs.cpio.gz