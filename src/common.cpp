#import "common.h"
#include <sys/time.h>

#define MICROSECONDS_PER_SECOND (1000 * 1000)

uint64_t timestamp_now()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec * MICROSECONDS_PER_SECOND + tv.tv_usec);
}