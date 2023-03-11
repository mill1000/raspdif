#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"

static log_level_t min_level = log_level_info;

/**
  @brief  Set the minimum logging level

  @param  level Minimum log level to print
  @retval none
*/
void log_set_level(log_level_t level)
{
  min_level = level;
}

/**
  @brief  Print to the log at the target level

  @param  level Level to print at
  @param  format Format string to print
  @param  ... VA list
  @retval none
*/
void __attribute__((weak)) log_print(log_level_t level, const char* format, ...)
{
  if (level < min_level)
    return;

  va_list list;
  va_start(list, format);
  vprintf(format, list);
  va_end(list);

  if (level == log_level_fatal)
    exit(EXIT_FAILURE);
}