/**
 * @file jr_strings.c
 * @author Roman Volkov aka Jolly_Roger000 (j.roger@internet.ru) https://github.com/JollyRoger000
 * @brief Файл реализации функций работы со строками
 * @version 0.1
 * @date 2024-09-15
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "jr_strings.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

/**
 * @brief Функция динамического выделения данных под строку
 * 
 * @param source  Исходная строка
 * @return char* Выходная строка
 *
 * @example
 * char *str = _string("Hello, World!");
 */
char *_string(const char *source)
{
    const char *tag = "_string";
    if (source)
    {
        uint32_t len = strlen(source);

        char *ret = (char *)malloc(len + 1);
        if (ret == NULL)
        {
            ESP_LOGE(tag, "Failed to create string: out of memory!");
            return NULL;
        }
        memset(ret, 0, len + 1);
        strcpy(ret, source);
        return ret;
    };
    return NULL;
}

/**
 * @brief Функция преобразования даты в строку в соответствии с форматом
 * 
 * @param format Формат строки 
 * @param value Время
 * @param bufsize Размер буфера
 * @return char* Выходная строка с временем
 *
 * @example
 * char *time_str = _timestr("%Y-%m-%d %H:%M:%S", time(NULL), 32);
 */
char *_timestr(const char *format, time_t value, int bufsize)
{
  struct tm timeinfo;
  localtime_r(&value, &timeinfo);
  char buffer[bufsize];
  memset(buffer, 0, sizeof(buffer));
  strftime(buffer, sizeof(buffer), format, &timeinfo);
  return _string(buffer);
}

