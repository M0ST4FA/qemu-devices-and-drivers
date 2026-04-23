#!/bin/sh

# SOCK=/home/m0st4fa/workspace/kernel/learning/lkmpg/vfs.sock
#SOCK=/tmp/vfs.sock
SHARE=/home/m0st4fa/workspace/kernel/learning/lkmpg

KERNEL_PATH=$WS/kernel/linux-build/amd64-debug/arch/x86/boot/bzImage

#ROOTFS_PATH=../../artifacts/images/gentoo-amd64-from_stage3.img
ROOTFS_PATH=$WS/kernel/artifacts/images/gentoo-btrfs-disks
INITRD_PATH=$WS/kernel/artifacts/images/initramfs-amd64.cpio
ROOTFS_FLAGS="rootdelay=10 rootfstype=btrfs rootflags=subvol=/gentoo,device=/dev/vda,device=/dev/vdb,device=/dev/vdc"

if [[ $1 == "debug" ]]; then
	DEBUG_FLAGS="-s"
else
	DEBUG_FLAGS=""
fi

qemu-system-x86_64 \
  -machine q35 -cpu host -smp 4 -accel kvm -m 4G \
  -object memory-backend-memfd,id=mem,size=4G,share=on \
  -numa node,memdev=mem \
  -nodefaults \
  -chardev vc,id=mon0 -mon chardev=mon0,mode=readline \
  \
  -device virtio-serial-pci \
  -chardev socket,path=/tmp/qga.sock,server=on,wait=off,id=qga0 \
  -device virtserialport,chardev=qga0,name=org.qemu.guest_agent.0 \
  -chardev vc,id=hvc0 -device virtconsole,chardev=hvc0 \
  -chardev vc,id=hvc1 -device virtconsole,chardev=hvc1 \
  -chardev vc,id=hvc2 -device virtconsole,chardev=hvc2 \
  \
  -drive  if=virtio,format=raw,file=${ROOTFS_PATH}/btrfs1 \
  -drive  if=virtio,format=raw,file=${ROOTFS_PATH}/btrfs2 \
  -drive  if=virtio,format=raw,file=${ROOTFS_PATH}/btrfs3 \
  -fsdev  local,id=fsdev0,path=./,security_model=none \
  -fsdev  local,id=fsdev1,path=$WS/kernel/linux-build/amd64-debug,security_model=none \
  -device virtio-9p-pci,fsdev=fsdev0,mount_tag=mods9p \
  -device virtio-9p-pci,fsdev=fsdev1,mount_tag=kernel9p \
  \
  -device pcie-root-port,bus=pcie.0,id=rp1,chassis=1,slot=1 \
  -device pcie-root-port,bus=pcie.0,id=rp2,chassis=2,slot=2 \
  -device pcie-root-port,bus=pcie.0,id=rp3,chassis=3,slot=3 \
  -netdev user,id=net0 \
  \
  -device virtio-net-pci,netdev=net0 \
  -device edu \
  -device '{"driver":"vfio-user-pci","socket":{"path":"/tmp/math0.sock", "type":"unix"},"addr":"0f.0"}' \
  -device '{"driver":"vfio-user-pci","socket":{"path":"/tmp/math1.sock", "type":"unix"},"addr":"10.0"}' \
  -device '{"driver":"vfio-user-pci","socket":{"path":"/tmp/math2.sock", "type":"unix"},"multifunction":true,"addr":"0c.0"}' \
  -device '{"driver":"vfio-user-pci","socket":{"path":"/tmp/math3.sock", "type":"unix"},"addr":"0c.1"}' \
  \
  -kernel ${KERNEL_PATH} \
  -initrd ${INITRD_PATH} \
  -append "console=hvc0,115200 nokaslr rdinit=/init" ${DEBUG_FLAGS}

