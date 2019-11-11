#ifndef _PG_DEBUG_H
#define _PG_DEBUG_H

/*
#define PG2_DEBUG
*/

#ifndef _WIN32

#define DEBUG_PREFIX "\33[38;5;13mpg (debug) - \33[38;5;15m"
#define DEBUG_SUFFIX "\33[0m"

#else

#define DEBUG_PREFIX "pg (debug) - "
#define DEBUG_SUFFIX ""

#endif

#ifdef PG2_DEBUG

#define DEBUG_PRINT(msg) printf(DEBUG_PREFIX msg DEBUG_SUFFIX)
#define DEBUG_PRINTF(msg, ...) printf(DEBUG_PREFIX msg DEBUG_SUFFIX, __VA_ARGS__)

#else

#define DEBUG_PRINT(msg)
#define DEBUG_PRINTF(msg, ...)

#endif

#endif
