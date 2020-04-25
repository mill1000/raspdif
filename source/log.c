#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "log.h"

static LOG_LEVEL minLevel = LOG_LEVEL_INFO;

/**
  @brief  Set the minimum logging level

  @param  level Minimum log level to print
  @retval none
*/
void logSetLevel(LOG_LEVEL level)
{
  minLevel = level;
}

/**
  @brief  Print to the log at the target level

  @param  level Level to print at
  @param  format Format string to print
  @param  ... VA list
  @retval none
*/
void  __attribute__((weak)) logPrint(LOG_LEVEL level, const char* format, ...)
{
  if (level < minLevel)
    return;
    
  va_list list;
  va_start(list, format);
  vprintf(format, list);
  va_end(list);

  if (level == LOG_LEVEL_FATAL)
    exit(EXIT_FAILURE);
}