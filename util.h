#ifndef utilh
#define utilh
#include <sys/sdt.h>
#include <sys/types.h>
extern int gverbose;
ssize_t bufferfill(int fd, u_char *__restrict dest, size_t size, int charmode);
void stopwatch_start(struct timespec *t);
u_long stopwatch_stop(struct timespec *t, int whisper_channel);
#ifdef CHAOS
int chaos_fail();
#endif
unsigned long mix(unsigned int seed, void *data, unsigned long size);
#define whisper(level, ...)                                                    \
  {                                                                            \
    if (level < gverbose)                                                      \
      fprintf(stderr, __VA_ARGS__);                                            \
  }
#define checkperror(...)                                                       \
  do {                                                                         \
    if (errno != 0)                                                            \
      perror(__VA_ARGS__);                                                     \
  } while (0);
#endif
