/**
 * @file jr_strings.h
 * @author Roman Volkov aka Jolly_Roger000 (j.roger@internet.ru) https://github.com/JollyRoger000
 * @brief Заголовочный файл описания функций работы с строками
 * @version 0.1
 * @date 2024-09-15
 *
 * @copyright Copyright (c) 2024
 *
 */

#include <time.h>

/**
 * @brief Функция динамического выделения данных под строку
 *
 * @param source  Исходная строка
 * @return char* Выходная строка
 *
 * @example
 * char *str = _string("Hello, World!");
 */
char *_string(const char *source);

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
char *_timestr(const char *format, time_t value, int bufsize);