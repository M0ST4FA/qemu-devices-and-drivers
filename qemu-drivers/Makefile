# Kbuild
ALL_MODULES := pci-driver/ qemu-edu-driver/ mathaccel/

SELECTED := $(foreach dir,${SUB_TARGETS},${dir}/)
FINAL_OBJ := $(if ${SUB_TARGETS},$(filter ${SELECTED},${ALL_MODULES}),${ALL_MODULES})

obj-m := ${FINAL_OBJ}

# Non-Kbuild
KBUILD_MAKEFILE := /home/m0st4fa/workspace/kernel/linux-build/amd64-debug/Makefile
MODULE_PATH := ${CURDIR}
BUILD_DIR := 

.PHONY: all modules clean

all: modules

modules:
	@echo "Kbuild: Building ${obj-m}"
	${MAKE} -f ${KBUILD_MAKEFILE} M=${MODULE_PATH} MO=${BUILD_DIR}/${@D} modules

clean:
	${MAKE} -f ${KBUILD_MAKEFILE} M=${MODULE_PATH} MO=${BUILD_DIR}/${@D} clean
