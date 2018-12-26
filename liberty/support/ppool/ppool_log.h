#ifndef LOG_H
#define LOG_H
#include <stdio.h>

#ifndef NDEBUG
#define LOG(fmt, ...) \
		{fprintf (stderr, "LOG: %s:%d: ", __func__,__LINE__); \
		 fprintf (stderr, fmt, ## __VA_ARGS__);}     
#else
#define LOG(fmt, ...)
#endif

#endif
