#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#include <asm/types.h>

#define EDU_IOCTL_BASE 'e'

#define EDU_IOCTL_IDENT _IOR(EDU_IOCTL_BASE, 1, __u32)
#define EDU_IOCTL_LIVENESS _IOR(EDU_IOCTL_BASE, 2, __u32)
