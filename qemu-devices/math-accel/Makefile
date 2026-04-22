TARGET := $(notdir ${CURDIR})

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

CFLAGS +=
LDFLAGS +=

.PHONY: all clean

${TARGET}: ${OBJS}
	@mkdir -p ${BUILD_DIR}/${TARGET}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${BUILD_DIR}/${TARGET}/$@ ${BUILD_DIR}/${TARGET}/$^

%.o: %.c
	@mkdir -p ${BUILD_DIR}/${TARGET}
	${CC} ${CFLAGS} -c $< -o ${BUILD_DIR}/${TARGET}/$@

clean:
	echo "Cleaning..."
	rm -rf ${BUILD_DIR}/${TARGET}
