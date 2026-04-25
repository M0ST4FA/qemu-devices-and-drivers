ROOT_DIR := $(abspath .)
LIBVFIO_INC := ${ROOT_DIR}/libvfio-user/include
LIBVFIO_LIB := ${ROOT_DIR}/libvfio-user/build/lib

# BUILD_DIR := ${ROOT_DIR}/build
CFLAGS += -I${LIBVFIO_INC}
LDFLAGS += -L${LIBVFIO_LIB} -lvfio-user -Wl,-rpath=${LIBVFIO_LIB}

MAKEFILE_DIRS := $(patsubst %Makefile, %, $(wildcard */Makefile))
ALL_DEVICES := $(filter-out libvfio-user/, ${MAKEFILE_DIRS})
ALL_DEVICES := $(filter-out libvfio-user/, ${ALL_DEVICES})

TARGET_DEVICES := $(if ${SUB_TARGETS},$(filter ${SUB_TARGETS},${ALL_DEVICES}),${ALL_DEVICES})

.PHONY: all clean ${ALL_DEVICES}

all: ${TARGET_DEVICES}

${ALL_DEVICES}:
	@echo "==> Building device: $@"
	$(MAKE) BUILD_DIR=${BUILD_DIR} -C $@ 

clean:
	@for dir in ${TARGET_DEVICES}; do \
		echo "==> Cleaning device: $${dir}"; \
		$(MAKE) BUILD_DIR=${BUILD_DIR} -C $${dir} clean; \
	done
