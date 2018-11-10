#include "interfaces.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#define PG_VERSION "1.0.0-development"
#define PG_VERSION_MAJOR "1"
#define PG_VERSION_MINOR "0"
#define PG_VERSION_PATCH "0"
#define PG_VERSION_SUFFX "development"

#define MAX_PG_TYPES 131070