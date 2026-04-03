/*
 * log.h
 *
 *  Created on: Oct 2, 2025
 *      Author: Daruin Solano
 */

#ifndef LOG_H_
#define LOG_H_

#include <stdio.h>
#include "ansi.h"
#include "stm32l4xx_hal.h"   // Or your series, for HAL_GetTick()

// Log levels
#define LOG_LEVEL_NONE   0
#define LOG_LEVEL_ERROR  1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_INFO   3
#define LOG_LEVEL_DEBUG  4

// Set the global active level
#define LOG_ACTIVE_LEVEL LOG_LEVEL_DEBUG

// Internal helper: Print timestamp + file + line
#define LOG_META(level_color, level_name, fmt, ...) \
    printf(level_color "[%dms] %s:%d [%s] " fmt ANSI_RESET "\r\n", \
           (int)HAL_GetTick(), __func__, __LINE__, level_name, ##__VA_ARGS__)

// Public logging macros
#define LOG_PRINT(fmt, ...)  do { if (LOG_ACTIVE_LEVEL >= LOG_LEVEL_NONE)  LOG_META(ANSI_WHITE,    "PRINT", fmt, ##__VA_ARGS__); } while (0)
#define LOG_ERROR(fmt, ...)  do { if (LOG_ACTIVE_LEVEL >= LOG_LEVEL_ERROR)  LOG_META(ANSI_RED,    "ERROR", fmt, ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmt, ...)   do { if (LOG_ACTIVE_LEVEL >= LOG_LEVEL_WARN)   LOG_META(ANSI_YELLOW, "WARN ", fmt, ##__VA_ARGS__); } while (0)
#define LOG_INFO(fmt, ...)   do { if (LOG_ACTIVE_LEVEL >= LOG_LEVEL_INFO)   LOG_META(ANSI_GREEN,  "INFO ", fmt, ##__VA_ARGS__); } while (0)
#define LOG_DEBUG(fmt, ...)  do { if (LOG_ACTIVE_LEVEL >= LOG_LEVEL_DEBUG)  LOG_META(ANSI_CYAN,   "DEBUG", fmt, ##__VA_ARGS__); } while (0)




#endif /* LOG_H_ */
