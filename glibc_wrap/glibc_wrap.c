// These functions are only called by our code when built on newer distros. We
// add '-Wl,--wrap={funcname}' to the link line which changes all calls to
// '{funcname}' to '__wrap_{funcname}'

// For all the gritty details:
// https://www.win.tue.nl/~aeb/linux/misc/gcc-semibug.html
// https://stackoverflow.com/questions/4032373/linking-against-an-old-version-of-libc-to-provide-greater-application-coverage

#include <linux/unistd.h>
#include <math.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef __arm__
// Use GLIBC 2.2.5 versioning only if not on ARM
asm(".symver log2_glibc_225, log2@GLIBC_2.2.5");
extern double log2_glibc_225(double num);
#endif

double __wrap_log2(double num) {
#ifdef __arm__
  // If compiling for ARM, just call the system-native log2 function
  return log2(num);
#else
  // Call the GLIBC 2.2.5 version of log2 if not on ARM
  return log2_glibc_225(num);
#endif
}

pid_t __wrap_gettid(void) { return (pid_t)syscall(SYS_gettid); }
