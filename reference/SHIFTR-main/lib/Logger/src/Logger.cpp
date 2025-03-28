#include <Logger.h>
#include <cstdio>
#include <stdarg.h>

#ifdef CORE_DEBUG_LEVEL
int Logger::defaultLogLevel = CORE_DEBUG_LEVEL;
#else
int Logger::defaultLogLevel = LOG_LEVEL_VERBOSE;
#endif

/**
 * Logs a message including a new line
 * 
 * @param logLevel Log level
 * @param functionName Function name
 * @param fmt Format string
 * @param ... Arguments
 * @return Length of the logged message
 */
int Logger::logger_printf(int logLevel, const char *functionName, const char *fmt, ...) {
  int length = 0;
  if (Logger::defaultLogLevel >= logLevel) {
    length += printf("%s(): ", functionName);
    va_list args;
    va_start (args, fmt);
    length = vprintf(fmt, args);
    va_end (args);
    length += printf("\n");
  }
  return length;
}
