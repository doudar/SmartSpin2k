#ifndef LOGGER_H
#define LOGGER_H

#define LOG_LEVEL_NONE (0)
#define LOG_LEVEL_ERROR (1)
#define LOG_LEVEL_WARN (2)
#define LOG_LEVEL_INFO (3)
#define LOG_LEVEL_DEBUG (4)
#define LOG_LEVEL_VERBOSE (5)

#ifndef ESP32
#define log_e(format, __VA_ARGS__...) Logger::logger_printf(LOG_LEVEL_ERROR, __FUNCTION__, format, ##__VA_ARGS__)
#define log_w(format, __VA_ARGS__...) Logger::logger_printf(LOG_LEVEL_WARN, __FUNCTION__, format, ##__VA_ARGS__)
#define log_i(format, __VA_ARGS__...) Logger::logger_printf(LOG_LEVEL_INFO, __FUNCTION__, format, ##__VA_ARGS__)
#define log_d(format, __VA_ARGS__...) Logger::logger_printf(LOG_LEVEL_DEBUG, __FUNCTION__, format, ##__VA_ARGS__)
#define log_v(format, __VA_ARGS__...) Logger::logger_printf(LOG_LEVEL_VERBOSE, __FUNCTION__, format, ##__VA_ARGS__)
#endif

class Logger {
 public:
  static int defaultLogLevel;
  static int logger_printf(int logLevel, const char *functionName, const char *fmt, ...);
};

#endif