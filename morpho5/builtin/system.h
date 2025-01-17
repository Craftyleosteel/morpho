/** @file system.h
 *  @author T J Atherton
 *
 *  @brief Built in class to provide access to the runtime
 */

#ifndef system_h
#define system_h

#include <stdio.h>

#include "morpho.h"

/* -------------------------------------------------------
 * System class
 * ------------------------------------------------------- */

#define SYSTEM_CLASSNAME       "System"

#define SYSTEM_PLATFORM_METHOD        "platform"
#define SYSTEM_VERSION_METHOD         "version"
#define SYSTEM_CLOCK_METHOD           "clock"
#define SYSTEM_EXIT_METHOD            "exit"

#define SYSTEM_MACOS   "macos"
#define SYSTEM_LINUX   "linux"
#define SYSTEM_UNIX    "unix"
#define SYSTEM_WINDOWS "windows"

void system_initialize(void);
void system_finalize(void);

#endif /* system_h */
