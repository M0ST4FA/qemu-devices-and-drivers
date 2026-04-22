# Kbuild
ALL_MODULES := pci-driver/ qemu-edu-driver/ math-accel/

SELECTED := $(foreach dir,${SUB_TARGETS},${dir}/)
FINAL_OBJ := $(if ${SUB_TARGETS},$(filter ${SELECTED},${ALL_MODULES}),${ALL_MODULES})

obj-m := ${FINAL_OBJ}

# Non-Kbuild
KBUILD_MAKEFILE := /home/m0st4fa/workspace/kernel/linux-build/amd64-debug/Makefile
MODULE_PATH := ${CURDIR}
BUILD_DIR := 

PROJ_PATH := /home/m0st4fa/workspace/kernel/experiments/devices
BUILD_DIR := ${PROJ_PATH}/build

.PHONY: all modules pci-user clean bear

all: modules pci-user

modules:
	@echo "Kbuild: Building ${obj-m}"
	${MAKE} -f ${KBUILD_MAKEFILE} M=${MODULE_PATH} MO=${BUILD_DIR}/${@D} modules

pci-user: ./pci-driver/pci-user.c
	${CC} -o ${BUILD_DIR}/pci-user ./pci-driver/pci-user.c

clean:
	${MAKE} -f ${KBUILD_MAKEFILE} M=${MODULE_PATH} MO=${BUILD_DIR}/${@D} clean
	$(if $(or $(filter pci-driver/,${obj-m}), $(filter pci-user,${SUB_TARGETS})), rm -f ${BUILD_DIR}/pci-user)

bear:
	bear -- make --always-make all
