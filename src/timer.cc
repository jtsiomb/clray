#include "timer.h"

#if defined(unix) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/time.h>

long get_msec()
{
	static struct timeval tv0;
	struct timeval tv;

	gettimeofday(&tv, 0);

	if(tv0.tv_sec == 0 && tv0.tv_usec == 0) {
		tv0 = tv;
		return 0;
	}
	return (tv.tv_sec - tv0.tv_sec) * 1000L + (tv.tv_usec - tv0.tv_usec) / 1000L;
}

#elif defined(WIN32) || defined(__WIN32__)
#include <windows.h>

#pragma comment(lib, "winmm.lib")

long get_msec()
{
	return timeGetTime();
}
#endif
