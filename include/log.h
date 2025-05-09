#ifndef __LOG__
#define __LOG__

typedef enum
{
  log_level_debug = 0,
  log_level_info,
  log_level_warn,
  log_level_error,
  log_level_fatal,
} log_level_t;

#ifndef LOG_DISABLE_COLOR
#define LOG_COLOR_NONE   ""
#define LOG_COLOR_RED    "31"
#define LOG_COLOR_GREEN  "32"
#define LOG_COLOR_YELLOW "33"
#define LOG_COLOR(COLOR) "\033[0;" COLOR "m"
#define LOG_RESET_COLOR  "\033[0m"
#else
#define LOG_COLOR_NONE
#define LOG_COLOR_RED
#define LOG_COLOR_GREEN
#define LOG_COLOR_YELLOW
#define LOG_COLOR(COLOR)
#define LOG_RESET_COLOR
#endif

#define LOG_FORMAT(COLOR, LETTER, FORMAT) \
  LOG_COLOR(COLOR)                        \
  #LETTER ": %s: " FORMAT LOG_RESET_COLOR "\n"

#define _LOG(LEVEL, FORMAT, ...)             \
  do {                                       \
    log_print(LEVEL, FORMAT, ##__VA_ARGS__); \
  } while (0)

#define LOGD(TAG, FORMAT, ...) _LOG(log_level_debug, LOG_FORMAT(LOG_COLOR_NONE, D, FORMAT), TAG, ##__VA_ARGS__)
#define LOGI(TAG, FORMAT, ...) _LOG(log_level_info, LOG_FORMAT(LOG_COLOR_GREEN, I, FORMAT), TAG, ##__VA_ARGS__)
#define LOGW(TAG, FORMAT, ...) _LOG(log_level_warn, LOG_FORMAT(LOG_COLOR_YELLOW, W, FORMAT), TAG, ##__VA_ARGS__)
#define LOGE(TAG, FORMAT, ...) _LOG(log_level_error, LOG_FORMAT(LOG_COLOR_RED, E, FORMAT), TAG, ##__VA_ARGS__)
#define LOGF(TAG, FORMAT, ...) _LOG(log_level_fatal, LOG_FORMAT(LOG_COLOR_RED, F, FORMAT), TAG, ##__VA_ARGS__)

void log_print(log_level_t level, const char* format, ...);
void log_set_level(log_level_t level);

#endif