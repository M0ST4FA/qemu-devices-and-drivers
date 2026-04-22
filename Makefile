export CC := gcc
export CFLAGS += -Wall -Wextra -g -O0
export LDFLAGS +=

PROJ_PATH := /home/m0st4fa/workspace/kernel/experiments/devices

KBUILD_MAKEFILE := /home/m0st4fa/workspace/kernel/linux-build/amd64-debug/Makefile
MODULE_PATH := ${CURDIR}

BUILD_DIR := ${PROJ_PATH}/build
SUBDIRS := "qemu-drivers" "qemu-devices"

MODULE_NAMES := $(filter-out modules,${MAKECMDGOALS})
DEVICE_NAMES := $(filter-out devices,${MAKECMDGOALS})
CLEAN_NAMES := $(filter-out all clean modules devices pci-user bear run-qemu run-qemu-debug,${MAKECMDGOALS})

.PHONY: all modules pci-user devices clean bear debug run-qemu run-qemu-debug

# My Makefile
all: modules pci-user devices

modules:
	${MAKE} -C ./qemu-drivers BUILD_DIR=${BUILD_DIR} modules SUB_TARGETS=${MODULE_NAMES}

pci-user:
	${MAKE} -C ./qemu-drivers BUILD_DIR=${BUILD_DIR} pci-user

devices:
	${MAKE} -C ./qemu-devices BUILD_DIR=${BUILD_DIR} all SUB_TARGETS=${DEVICE_NAMES}

clean:
	@for dir in ${SUBDIRS}; do \
		echo "==> Cleaning in $${dir}: $(if ${CLEAN_NAMES},${CLEAN_NAMES},all)"; \
		${MAKE} BUILD_DIR=${BUILD_DIR} -C $${dir} clean SUB_TARGETS="${CLEAN_NAMES}"; \
	done

debug:
	gdb ~/workspace/kernel/linux-build/amd64-debug/vmlinux.unstripped

run-qemu:
	./run-qemu.sh

run-qemu-debug:
	./run-qemu.sh debug

bear:
	make clean
	bear -- make

# Only create no-op rules for words that are NOT main targets.
# This prevents "overriding recipe" warnings for 'modules', 'clean', etc.
ARGS := $(filter-out all modules devices pci-user clean bear debug run-qemu run-qemu-debug,$(MAKECMDGOALS))

$(ARGS):
	@:
