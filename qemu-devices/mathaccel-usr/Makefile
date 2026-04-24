TARGET := $(notdir ${CURDIR})
TARGET_DIR := ${BUILD_DIR}/${TARGET}

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c, ${TARGET_DIR}/%.o, ${SRCS})

ABS := $(abspath .)

CFLAGS += -I${ABS}/include
LDFLAGS +=

.PHONY: all clean

all: ${TARGET_DIR}/${TARGET}

${TARGET_DIR}/${TARGET}: ${OBJS}
	@mkdir -p $(dir $@)
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $^

${TARGET_DIR}/%.o: %.c
	@mkdir -p $(dir $@)
	${CC} ${CFLAGS} -o $@ -c $^

clean:
	echo "Cleaning..."
	rm -rf ${TARGET_DIR}
