#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_smartconfig.h"
#include "mqtt_client.h"
#include "esp_sntp.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_mac.h"
#include <cJSON.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include "esp_tls.h"
#include <esp_http_server.h>
#include "esp_spiffs.h"

#include "config.h"

#define SM_DIR 19
#define SM_STEP 17
#define SM_nEN 18
#define SW 22
#define LED_STATUS 23
#define SERVICE_BTN 13

extern const uint8_t wqtt_pem_start[] asm("_binary_wqtt_pem_start");
extern const uint8_t wqtt_pem_end[] asm("_binary_wqtt_pem_end");

extern const uint8_t owmap_org_pem_start[] asm("_binary_owmap_org_pem_start");
extern const uint8_t owmap_org_pem_end[] asm("_binary_owmap_org_pem_end");

extern const uint8_t tg_org_pem_start[] asm("_binary_api_telegram_org_pem_start");
extern const uint8_t tg_org_pem_end[] asm("_binary_api_telegram_org_pem_end");

extern const uint8_t ss_pem_start[] asm("_binary_sunrise_sunset_org_pem_start");
extern const uint8_t ss_pem_end[] asm("_binary_sunrise_sunset_org_pem_end");

#define DEFAULT_MAX_TIME_SYNC_WAITING 10
#define DEFAULT_MAX_STEPS 30000
#define MAX_RECONNECT_ATTEMPTS 5 /**< Максимальное количество попыток подключения к MQTT-брокеру. */
#define RECONNECT_DELAY_MS 1000 /**< Задержка между попытками повторного подключения в миллисекундах. */

#define WIFI_START_BIT BIT0     // Бит запуска подключения к WiFi
#define WIFI_DONE_BIT BIT1      // Бит успешного подключения к WiFi
#define WIFI_FAIL_BIT BIT2      // Бит ошибки подключения (выставляется при ошибке подключения заданное число раз)
#define SC_START_BIT BIT3       // Бит запуска smartconfig
#define SC_DONE_BIT BIT4        // Бит успешнго завершения smartconfig
#define SC_FOUND_BIT BIT5       // Бит обнаружения смартфона SC
#define REINIT_BIT BIT6         // Бит переинициализации системы
#define ERR_OW_BIT BIT7         // Бит ошибки получения данных openweather
#define ERR_TG_BIT BIT8         // Бит ошибки подключения к telegram
#define ERR_MQTT_BIT BIT9       // Бит ошибки подключения к MQTT
#define ERR_TIME_SYNC_BIT BIT10 // Бит ошибки синхронизации времени
#define OTA_START_BIT BIT11     // Бит начала процесса обновления
#define OTA_CONNECT_BIT BIT12   // Бит подключения к серверу
#define OTA_FINISH_BIT BIT13    // Бит завершения обновления

const int mqttPort = WQTT_PORT;
const int mqttTlsPort = WQTT_TLS_PORT;
const char *mqttServer = WQTT_SERVER;
const char *mqttsServer = WQTT_TLS_SERVER;
const char *mqttUser = WQTT_USER;
const char *mqttPass = WQTT_PASSWORD;

const int mqttReservePort = LOCAL_MOSQUITTO_PORT;
const char *mqttReserveServer = LOCAL_MOSQUITTO_SERVER;
const char *mqttReserveUser = LOCAL_MOSQUITTO_USER;
const char *mqttReservePass = LOCAL_MOSQUITTO_PASSWORD;

static char *mqttPrefix = NULL;
static char *mqttTopicCheckOnline = NULL;
static char *mqttTopicControl = NULL;
static char *mqttTopicStatus = NULL;
static char *mqttTopicTimers = NULL;
static char *mqttTopicAddTimer = NULL;
static char *mqttTopicAddSunrise = NULL;
static char *mqttTopicAddSunset = NULL;
static char *mqttTopicDelSunrise = NULL;
static char *mqttTopicDelSunset = NULL;
static char *mqttTopicSystem = NULL;
static char *mqttTopicSystemUpdate = NULL;
static char *mqttTopicSystemMaxSteps = NULL;
static char *mqttTopicSystemTGKey = NULL;
static char *mqttTopicSystemOWKey = NULL;
static char *mqttTopicSystemServerTime1 = NULL;
static char *mqttTopicSystemServerTime2 = NULL;
static char *mqttTopicSystemTimeZone = NULL;
static char *mqttTopicSystemCountry = NULL;
static char *mqttTopicSystemCity = NULL;
static char *tgMessage = NULL;

static int mqttTopicStatusQoS = 0;
static int mqttTopicCheckOnlineQoS = 0;
static int mqttTopicControlQoS = 0;
static int mqttTopicTimersQoS = 0;
static int mqttTopicAddTimerQoS = 0;
static int mqttTopicAddSunriseQoS = 0;
static int mqttTopicAddSunsetQoS = 0;
static int mqttTopicDelSunriseQoS = 0;
static int mqttTopicDelSunsetQoS = 0;
static int mqttTopicSystemQoS = 0;
static int mqttTopicSystemUpdateQoS = 0;
static int mqttTopicSystemMaxStepsQoS = 0;
static int mqttTopicSystemTGKeyQoS = 0;
static int mqttTopicSystemOWKeyQoS = 0;
static int mqttTopicSystemServerTime1QoS = 0;
static int mqttTopicSystemServerTime2QoS = 0;
static int mqttTopicSystemTimeZoneQoS = 0;
static int mqttTopicSystemCountryQoS = 0;
static int mqttTopicSystemCityQoS = 0;

static int mqttTopicStatusRet = 0;
static int mqttTopicCheckOnlineRet = 0;
static int mqttTopicSystemRet = 0;

char *location = NULL;
char *frendly_name = NULL;
char *sunrise = NULL;
char *sunset = NULL;
char *last_ow_updated = NULL;
char *last_started = NULL;
char *ow_key = NULL;
char *tg_bot_token = NULL;
char *city = NULL;
char *country = NULL;
char *update_url = NULL;
char *timezone = NULL;
char *time_server1 = NULL;
char *time_server2 = NULL;
char *last_updated = NULL;
char *ssid = NULL;
char *password = NULL;
char *ip = NULL;
char *move_status = NULL;

char *app_name = NULL;
char *app_version = NULL;
char *app_date = NULL;
char *app_time = NULL;

time_t sunrise_time = 0;
time_t sunset_time = 0;
uint8_t move_on_sunrise = 0;
uint8_t move_on_sunset = 0;
uint8_t shade_sunrise = 0;
uint8_t shade_sunset = 0;
uint8_t shade = 0;
uint8_t calibrate = 0;
uint16_t target_pos = 0;
uint16_t current_pos = 0;
uint16_t length = 0;
uint64_t working_time = 0;

static EventGroupHandle_t event_group = NULL; // Группа событий
static TaskHandle_t calibrate_task_handle = NULL;
static TaskHandle_t move_task_handle = NULL;
static TimerHandle_t timer1_handle = NULL;
static esp_mqtt_client_handle_t mqttClient = NULL;
static wifi_config_t wifi_config; // Структура для хранения настроек WIFI

static int connect_retry = 0;
static int max_connect_retry = 10;
static int wating_to_time_sync = 0;

static char *ow_data = NULL;
static size_t ow_len = 0;

static uint16_t calibrateCnt = 0;

static bool ssid_loaded = false;
static bool password_loaded = false;
static bool time_sync = false;
static bool mqttConnected = false;
static bool isStarted = false;
static size_t ssid_size = 0;
static size_t password_size = 0;

static char *mqttHostname = NULL;

static void mqtt_start(void);
static void time_sync_start(const char *tz);
static void time_sync_cb(struct timeval *tv);
static void timer1_cb(TimerHandle_t pxTimer);
static void onCalibrate();
static void onStop();
static void onShade();
static void move_task(void *param);
static void calibrate_task(void *param);
static void smartconfig_task(void *param);
static void openweather_task(void *param);
static void publish_task(void *params);
static void ota_task(void *param);
static void led_task(void *param);
static void init_btn_task(void *param);
static bool mqttPublish(esp_mqtt_client_handle_t client, char *topic, char *data, int qos, int retain);
static char *mqttStatusJson();
static char *mqttSystemJson();
static esp_err_t nvs_write_u8(char *key, uint8_t val);
static esp_err_t nvs_write_u16(char *key, uint16_t val);
static esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client);
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static void sc_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ota_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void handler_on_wifi_disconnect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void handler_on_wifi_connect(void *esp_netif, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void handler_on_sta_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static httpd_handle_t server_setup(void);

// Функция инициализации spiffs
static void init_spiffs(void)
{
    const char *tag = "init_spiffs";

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(tag, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(tag, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(tag, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(tag, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(tag, "Partition size: total: %d, used: %d", total, used);
    }
}

// Функция для настройки HTTP-сервера
esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *tag = "root_get_handler";
    const char *path = "/spiffs/index.html"; // Путь к файлу

    // Проверка существования файла index.html
    struct stat st;
    if (stat(path, &st) != 0)
    {
        ESP_LOGE(tag, "index.html not found");
        httpd_resp_send_404(req); // Отправка 404, если файл не найден
        return ESP_FAIL;
    }

    ESP_LOGI(tag, "index.html found, size: %ld bytes", st.st_size);

    // Открытие файла и отправка его содержимого
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
    {
        ESP_LOGE(tag, "Failed to open index.html for reading");
        httpd_resp_send_500(req); // Серверная ошибка, если открытие файла не удалось
        return ESP_FAIL;
    }

    char buffer[512];
    size_t read_bytes;
    httpd_resp_set_type(req, "text/html");

    // Чтение файла и отправка содержимого
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        httpd_resp_send_chunk(req, buffer, read_bytes);
    }

    fclose(fp);                          // Закрытие файла
    httpd_resp_send_chunk(req, NULL, 0); // Завершение ответа
    return ESP_OK;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

esp_err_t data_post_handler(httpd_req_t *req)
{
    const char *TAG = "data_post_handler";
    char buf[512];
    int ret, remaining = req->content_len;

    // Чтение тела запроса
    while (remaining > 0)
    {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret < 0)
        {
            ESP_LOGE(TAG, "Error receiving data");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error receiving data");
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    // Парсинг JSON
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to parse JSON");
        return ESP_FAIL;
    }

    // Получение данных
    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *password = cJSON_GetObjectItem(json, "password");
    cJSON *telegramToken = cJSON_GetObjectItem(json, "telegramToken");
    cJSON *mqttServer = cJSON_GetObjectItem(json, "mqttServer");
    cJSON *mqttPort = cJSON_GetObjectItem(json, "mqttPort");
    cJSON *mqttPassword = cJSON_GetObjectItem(json, "mqttPassword");
    cJSON *sendInterval = cJSON_GetObjectItem(json, "sendInterval");

    // Проверка на валидность данных
    // if (ssid && cJSON_IsString(ssid) &&
    //     password && cJSON_IsString(password) &&
    //     telegramToken && cJSON_IsString(telegramToken) &&
    //     mqttServer && cJSON_IsString(mqttServer) &&
    //     mqttPort && cJSON_IsNumber(mqttPort) &&
    //     mqttPassword && cJSON_IsString(mqttPassword) &&
    //     sendInterval && cJSON_IsNumber(sendInterval))
    // {

    ESP_LOGI(TAG, "Received SSID: %s, Password: %s, Telegram Token: %s, MQTT Server: %s, MQTT Port: %d, MQTT Password: %s, Send Interval: %d",
             ssid->valuestring, password->valuestring, telegramToken->valuestring,
             mqttServer->valuestring, mqttPort->valueint, mqttPassword->valuestring, sendInterval->valueint);

    // Здесь можно добавить логику для подключения к Wi-Fi и MQTT с использованием полученных данных

    // Формирование успешного ответа
    const char *resp_msg = "{\"message\":\"Данные успешно получены!\"}";
    httpd_resp_send(req, resp_msg, strlen(resp_msg));
    // }
    // else
    // {
    //     ESP_LOGE(TAG, "Invalid input data");
    //     cJSON_Delete(json);
    //     httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid input data");
    //     return ESP_FAIL;
    // }

    cJSON_Delete(json); // Удаление парсенного JSON
    return ESP_OK;
}

esp_err_t update_firmware_handler(httpd_req_t *req)
{
    const char *TAG = "update_firmware_handler";
    char buf[512];
    int ret, remaining = req->content_len;

    // Чтение тела запроса
    while (remaining > 0)
    {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret < 0)
        {
            ESP_LOGE(TAG, "Error receiving data");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error receiving data");
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    // Парсинг JSON
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to parse JSON");
        return ESP_FAIL;
    }

    cJSON *updateServer = cJSON_GetObjectItem(json, "updateServer");
    if (updateServer && cJSON_IsString(updateServer))
    {
        ESP_LOGI(TAG, "Updating firmware from: %s", updateServer->valuestring);

        // Здесь должна быть логика для обновления прошивки
        // Например, загрузка файла из updateServer

        // Формирование успешного ответа
        const char *resp_msg = "{\"message\":\"Прошивка успешно обновлена!\"}";
        httpd_resp_send(req, resp_msg, strlen(resp_msg));
    }
    else
    {
        ESP_LOGE(TAG, "Invalid update server URL");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid update server URL");
        return ESP_FAIL;
    }

    cJSON_Delete(json); // Удаление парсенного JSON
    return ESP_OK;
}

// Обработчик обновления поля
esp_err_t update_handler(httpd_req_t *req)
{
    const char *TAG = "update_field_handler";
    char buf[100];
    int ret, remaining = req->content_len;

    // Считываем тело запроса
    while (remaining > 0)
    {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    // Преобразуем тело запроса в JSON
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Извлекаем поле и его значение
    const cJSON *fieldId = cJSON_GetObjectItemCaseSensitive(json, "fieldId");
    const cJSON *fieldValue = cJSON_GetObjectItemCaseSensitive(json, "fieldValue");

    if (!cJSON_IsString(fieldId) || !cJSON_IsString(fieldValue))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid fieldId or fieldValue");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Логика обработки обновлений
    ESP_LOGI(TAG, "Update param: %s new value: %s", fieldId->valuestring, fieldValue->valuestring);
    // Здесь вы можете добавить логику для сохранения обновлений в Flash или другую логику

    // Возвращаем успешный ответ
    const char *resp_str = "{\"message\": \"Обновление успешно выполнено.\"}";
    httpd_resp_sendstr(req, resp_str);
    cJSON_Delete(json);
    return ESP_OK;
}

httpd_handle_t start_webserver(void)
{
    const char *TAG = "start_webserver";
    ESP_LOGI(TAG, "Starting server..."); // Лог информирования о запуске сервера

    // Конфигурация сервера
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;     // Порт сервера
    config.max_uri_handlers = 8; // Максимальное количество обработчиков URI

    httpd_handle_t server = NULL;

    // Запуск HTTP-сервера
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Регистрация обработчика для корневой страницы
        httpd_uri_t uri_get = {
            .uri = "/",                  // URI для корневой страницы
            .method = HTTP_GET,          // Метод HTTP
            .handler = root_get_handler, // Обработчик запроса
            .user_ctx = NULL             // Дополнительный контекст (не используется)
        };

        // Регистрация обработчика для получения данных
        httpd_uri_t uri_update = {
            .uri = "/update",          // URI для получения данных
            .method = HTTP_POST,       // Метод HTTP
            .handler = update_handler, // Обработчик получения данных
            .user_ctx = NULL           // Дополнительный контекст (не используется)
        };

        // Регистрация обработчиков URI
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_update);

        ESP_LOGI(TAG, "Server started"); // Лог успешного запуска сервера
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start server"); // Сообщение об ошибке
    }

    return server; // Возврат дескриптора сервера
}

/**
 * @brief Создает дублирующую строку из переданного источника.
 *
 * Эта функция выделяет память для новой строки и копирует в нее содержимое
 * переданной строки. Если переданная строка является NULL, функция
 * вернет NULL. В случае ошибки при выделении памяти будет выведено
 * сообщение об ошибке в лог.
 *
 * @param source Указатель на строку, которую необходимо скопировать.
 * @return Указатель на новую строку при успехе; NULL в случае ошибки.
 */
char *_string(const char *source)
{
    const char *tag = "_string";
    if (source)
    {
        size_t len = strlen(source);

        char *ret = (char *)malloc(len + 1);
        if (ret == NULL)
        {
            ESP_LOGE(tag, "Failed to create string: out of memory!");
            return NULL;
        }

        // Использовать strncpy для большей безопасности
        strncpy(ret, source, len);
        ret[len] = '\0'; // гарантируем, что строка будет завершена нулем
        return ret;
    }

    ESP_LOGE(tag, "Source string is NULL.");
    return NULL;
}

/**
 * @brief Форматирует строку с переменным числом аргументов.
 *
 * Эта функция принимает формат строки и переменное количество аргументов,
 * создает строку с отформатированным содержимым и возвращает указатель на неё.
 * Если произойдет ошибка при выделении памяти или форматировании строки,
 * функция вернет NULL. Также будет выведено сообщение об ошибке в лог.
 *
 * @param format Указатель на строку формата.
 * @param ... Переменное количество аргументов для форматирования.
 * @return Указатель на отформатированную строку при успехе; NULL в случае ошибки.
 */
char *_stringf(const char *format, ...)
{
    const char *tag = "_stringf";
    char *ret = NULL;

    if (format != NULL)
    {
        va_list args1, args2;
        va_start(args1, format);
        va_copy(args2, args1);

        // Вычисление длины результирующей строки
        int len = vsnprintf(NULL, 0, format, args1);
        va_end(args1);

        if (len > 0) // Убедиться, что длина больше нуля
        {
            ret = (char *)malloc(len + 1);
            if (ret != NULL)
            {
                vsnprintf(ret, len + 1, format, args2);
            }
            else
            {
                ESP_LOGE(tag, "Failed to format string: out of memory!");
            }
        }
        else
        {
            ESP_LOGE(tag, "Formatting error, vsnprintf returned length: %d", len);
        }
        va_end(args2);
    }
    else
    {
        ESP_LOGE(tag, "Format string is NULL.");
    }

    return ret;
}

/**
 * @brief Создает дублирующую строку из переданной строки ограниченной длины.
 *
 * Эта функция выделяет память для новой строки и копирует в нее
 * содержимое переданной строки, не превышая заданную длину.
 * Если переданная строка является NULL или выделение памяти неудачно,
 * функция вернет NULL. В случае ошибки будет выведено сообщение
 * об ошибке в лог.
 *
 * @param source Указатель на строку, которую необходимо скопировать.
 * @param len Максимальная длина для копирования.
 * @return Указатель на новую строку при успехе; NULL в случае ошибки.
 */
char *_stringl(const char *source, const uint32_t len)
{
    const char *tag = "_stringl";
    if (source)
    {
        // Проверка длины исходной строки
        size_t sourceLen = strlen(source);
        size_t copyLen = len < sourceLen ? len : sourceLen; // должна быть длина к копированию
        char *ret = (char *)malloc(copyLen + 1);

        if (ret == NULL)
        {
            ESP_LOGE(tag, "Failed to create string: out of memory!");
            return NULL;
        }

        // Копируем и гарантируем завершение строки нулем
        strncpy(ret, source, copyLen);
        ret[copyLen] = '\0'; // Нужное завершение строки

        return ret;
    }

    ESP_LOGE(tag, "Source string is NULL.");
    return NULL;
}

/**
 * @brief Преобразует значение времени в строку заданного формата.
 *
 * Эта функция принимает значение времени в формате `time_t` и
 * форматную строку, а затем преобразует время в строку, используя
 * указанный формат. Функция возвращает указатель на созданную строку,
 * выделенную с использованием функции `_string`, или NULL в случае ошибки.
 *
 * @param format Формат строки для преобразования времени.
 * @param value Значение времени в формате `time_t`.
 * @param bufsize Размер буфера для временной строки.
 * @return Указатель на отформатированную строку при успехе; NULL в случае ошибки.
 */
char *_timestr(const char *format, time_t value, int bufsize)
{
    const char *tag = "_timestr";

    if (bufsize <= 0)
    {
        ESP_LOGE(tag, "Buffer size must be greater than zero.");
        return NULL;
    }

    struct tm timeinfo;
    if (localtime_r(&value, &timeinfo) == NULL)
    {
        ESP_LOGE(tag, "Failed to convert time value.");
        return NULL;
    }

    char buffer[bufsize];
    memset(buffer, 0, sizeof(buffer));

    if (strftime(buffer, sizeof(buffer), format, &timeinfo) == 0)
    {
        ESP_LOGE(tag, "Failed to format time string.");
        return NULL;
    }

    return _string(buffer);
}

/**
 * @brief Форматирует строку в массив символов с использованием переменного числа аргументов.
 *
 * Эта функция принимает буфер и форматную строку, а затем формирует строку,
 * используя переменные аргументы. Если буфер недостаточно велик для хранения
 * отформатированной строки, функция возвращает отрицательное значение,
 * и сообщение об ошибке будет записано в журнал.
 *
 * @param buffer Указатель на буфер, в который будет записана отформатированная строка.
 * @param buffer_size Размер переданного буфера.
 * @param format Формат, который будет использоваться для форматирования строки.
 * @param ... Переменное количество аргументов для форматирования.
 * @return Количество символов, записанных в буфер, или отрицательное значение при ошибке.
 */
uint16_t format_string(char *buffer, uint16_t buffer_size, const char *format, ...)
{
    const char *tag = "format_string";
    uint16_t ret = 0;

    if (!buffer || !format || buffer_size == 0)
    {
        ESP_LOGE(tag, "Invalid buffer or format string.");
        return 0; // Неверный ввод
    }

    // Получаем список аргументов
    va_list args;
    va_start(args, format);

    // Определяем необходимую длину буфера
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args); // Завершаем работу с args, чтобы избежать ошибок

    if (len < 0)
    {
        ESP_LOGE(tag, "Error in formatting string.");
        return 0; // Произошла ошибка
    }

    if (len + 1 > buffer_size) // +1 для нулевого символа
    {
        ESP_LOGE(tag, "Buffer %d bytes too small to hold formatted string, %d bytes needed", buffer_size, len + 1);
        return 0; // Буфер слишком мал
    }

    va_start(args, format); // Снова инициализируем список аргументов
    ret = vsnprintf(buffer, buffer_size, format, args);
    va_end(args); // Завершаем работу с args

    return ret > 0 ? ret : 0; // Возвращаем положительное значение или 0 при неудаче
}

/**
 * @brief Отправляет сообщение в Telegram через бот.
 *
 * Эта функция формирует JSON-запрос и отправляет его в Telegram API для пересылки сообщения.
 *
 * @param msg Указатель на строку, содержащую текст сообщения, которое будет отправлено.
 *            Не должен быть NULL.
 * @return
 * - `ESP_OK` в случае успешной отправки сообщения.
 * - `ESP_ERR_INVALID_ARG` если входящий параметр @p msg равен NULL.
 * - `ESP_ERR_NO_MEM` если не удалось выделить память для конфигурации HTTP-клиента.
 * - `ESP_FAIL` если произошла ошибка при форматировании сообщения или отправке запроса.
 */
static esp_err_t send_telegram_message(const char *msg)
{
    const char *tag = "send_telegram_message";

    if (!msg)
    {
        ESP_LOGE(tag, "Message cannot be NULL");
        return ESP_ERR_INVALID_ARG; // Неверный аргумент
    }

    char buf[2048];
    char buf_timestamp[20];
    char path[512];

    time_t now = time(NULL);
    strftime(buf_timestamp, sizeof(buf_timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    uint16_t size = format_string(buf, sizeof(buf),
                                  "{\"chat_id\":%s,\"parse_mode\":\"HTML\",\"disable_notification\":%s,\"text\":\"%s\\r\\n\\r\\n<code>%s</code>\"}",
                                  TELEGRAM_CHAT_ID,
                                  "false",
                                  msg,
                                  buf_timestamp);

    if (size == 0)
    {
        ESP_LOGE(tag, "Failed to format message");
        return ESP_FAIL; // Ошибка при форматировании сообщения
    }

    snprintf(path, sizeof(path), "https://api.telegram.org/bot%s/sendMessage", tg_bot_token);

    esp_http_client_config_t *cfg = malloc(sizeof(esp_http_client_config_t));
    if (!cfg)
    {
        ESP_LOGE(tag, "Failed to allocate memory for HTTP config");
        return ESP_ERR_NO_MEM; // Проверка выделения памяти
    }

    memset(cfg, 0, sizeof(esp_http_client_config_t)); // Инициализация нулями
    cfg->url = path;                                  // Установка полного URL
    cfg->method = HTTP_METHOD_POST;
    cfg->transport_type = HTTP_TRANSPORT_OVER_SSL;
    cfg->cert_pem = (char *)tg_org_pem_start; // Убедитесь, что сертификат корректен
    cfg->port = 443;

    esp_http_client_handle_t client = esp_http_client_init(cfg);
    free(cfg); // Освобождаем память для конфигурации

    if (client == NULL)
    {
        ESP_LOGE(tag, "HTTP client init error");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, buf, strlen(buf));

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(tag, "Send message error: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200)
    {
        ESP_LOGE(tag, "Server status code error: %d", status_code);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status_code == 200)
    {
        ESP_LOGI(tag, "Message sent successfully");
        return ESP_OK;
    }

    return ESP_FAIL; // Возвращаем ошибку при статусе != 200
}

#include "cJSON.h"

/**
 * @brief Обработчик событий HTTP-клиента.
 *
 * Этот обработчик обрабатывает различные события, полученные от HTTP-клиента,
 * включая получение данных, ошибки и состояния подключения.
 *
 * @param evt Указатель на событие HTTP-клиента.
 * @return esp_err_t Статус выполнения.
 */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    const char *TAG = "_http_event_handler";
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            // Обработка данных
            ESP_LOGI(TAG, "Получены данные: %.*s", evt->data_len, (char *)evt->data);
        }
        break;
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP Event Error");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP Event Connected");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP Event Disconnected");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * @brief Задача для выполнения HTTP-запросов к Telegram Bot API.
 *
 * Эта задача выполняет GET-запрос к Telegram Bot API с использованием
 * заданного токена бота. Запросы выполняются каждые 10 секунд.
 *
 * @param pvParameters Указатель на параметры задачи (не используется).
 */
void http_request_task(void *pvParameters)
{
    const char *TAG = "http_request_task";
    char url[256];

    // Формируем URL для получения обновлений из Telegram
    sprintf(url, "https://api.telegram.org/bot%s/getUpdates?offset=-1&timeout=60", tg_bot_token);

    while (1)
    {
        // Конфигурация HTTP-клиента
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = _http_event_handler,
            .cert_pem = (char *)tg_org_pem_start,      // Указание сертификата для SSL
            .transport_type = HTTP_TRANSPORT_OVER_SSL, // Использование SSL для запроса
            .port = 443,                               // Порт для HTTPS
        };

        // Инициализация HTTP-клиента
        esp_http_client_handle_t client = esp_http_client_init(&config);

        // Выполнение HTTP-запроса
        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld",
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));
        }
        else
        {
            ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        }

        // Освобождение ресурсов клиента
        esp_http_client_cleanup(client);

        // Задержка перед следующим запросом (10 секунд)
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/**
 * @brief Вычисляет процент свободной памяти в куче.
 *
 * Эта функция возвращает процент свободной памяти из общей памяти,
 * доступной в стандартной куче (MALLOC_CAP_DEFAULT).
 *
 * @return float Процент свободной памяти (0.0 до 100.0).
 */
static float esp_heap_free_percent()
{
    return 100.0 * ((float)heap_caps_get_free_size(MALLOC_CAP_DEFAULT) /
                    (float)heap_caps_get_total_size(MALLOC_CAP_DEFAULT));
}

// Функция калибровки длины шторы
static void onCalibrate()
{
    char *tag = "on_calibrate";
    ESP_LOGW(tag, "CALIBRATE message received");

    move_status = _string("calibrating");
    calibrate = 0;

    // Публикуем топик статуса
    char *str = mqttStatusJson();
    mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
    vPortFree(str);
    // Запускаем задачу калибровки
    xTaskCreate(calibrate_task, "calibrate_task", 4096, NULL, 3, &calibrate_task_handle);
}

// Функция останова вращения
static void onStop()
{
    char *tag = "on_stop";
    ESP_LOGW(tag, "STOP message received");

    // Если был запущен процесс калибровки, останавливаем и сохранаяем данные
    if (strcmp(move_status, "calibrating") == 0)
    {
        vTaskSuspend(calibrate_task_handle);
        gpio_set_level(LED_STATUS, 0);
        length = calibrateCnt;
        calibrate = 1;
        current_pos = length;
        target_pos = length;
        move_status = _string("stopped");

        ESP_LOGI(tag, "Calibrate success. Shade lenght is: %d", length);
        nvs_write_u16("length", length);
        nvs_write_u8("cal_status", calibrate);
        nvs_write_u16("current_pos", current_pos);
        nvs_write_u16("target_pos", target_pos);

        // Публикуем топик статуса
        char *str = mqttStatusJson();
        mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
        vPortFree(str);
    }
    else if (!strcmp(move_status, "opening") || !strcmp(move_status, "closing"))
    {
        vTaskSuspend(move_task_handle);
        gpio_set_level(LED_STATUS, 0);
        move_status = _string("stopped");

        target_pos = current_pos;
        nvs_write_u16("current_pos", current_pos);
        nvs_write_u16("target_pos", target_pos);

        // Публикуем топик статуса
        char *str = mqttStatusJson();
        mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
        vPortFree(str);
    }
    else
    {
        ESP_LOGE(tag, "unknown command");
    }
}

/**
 * @brief Функция обратного вызова для уведомления о синхронизации времени.
 *
 * Эта функция вызывается после успешной синхронизации времени с SNTP сервером.
 * Здесь можно добавить код для обработки времени, например, его вывод или
 * выполнение каких-либо действий по расписанию.
 */
void time_sync_cb(struct timeval *tv)
{
    const char *TAG = "time_sync_cb";
    ESP_LOGI(TAG, "Time synchronized: %lld seconds since epoch", tv->tv_sec);

    time_sync = true;
}

/**
 * @brief Запускает синхронизацию времени с SNTP сервером.
 *
 * Эта функция устанавливает указанный часовой пояс и инициализирует
 * синхронизацию времени через SNTP, включая конфигурацию SNTP сервера.
 *
 * @param tz Указатель на строку с названием часового пояса (например, "UTC" или "Europe/Moscow").
 */
static void time_sync_start(const char *tz)
{
    const char *tag = "time_sync_start";
    ESP_LOGI(tag, "started");

    // Устанавливаем часовой пояс
    setenv("TZ", tz, 1);
    tzset();

    // Настройка режима синхронизации SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // Установка SNTP серверов
    esp_sntp_setservername(0, time_server1);
    esp_sntp_setservername(1, time_server2);

    // Установка функции обратного вызова для уведомления о синхронизации времени
    sntp_set_time_sync_notification_cb(time_sync_cb);

    // Инициализация SNTP
    esp_sntp_init();
}

/**
 * @brief Создает JSON-строку со статусом системы.
 *
 * Функция собирает информацию о прошивке, времени, состоянии,
 * и других параметрах системы в формате JSON.
 *
 * @return Указатель на строку JSON. Должен быть освобожден вызовом free().
 *         Возвращает NULL в случае ошибки при создании JSON-объекта.
 */
static char *mqttStatusJson()
{
    char *str = NULL;

    cJSON *json = cJSON_CreateObject();
    if (json == NULL)
    {
        return NULL; // Обработка ошибки при создании объекта JSON
    }

    // Массив, содержащий ключи и соответствующие значения для прошивки
    struct
    {
        const char *key;
        const char *value;
    } firmwarePairs[] = {
        {"Firmware name", app_name},
        {"Firmware version", app_version},
        {"Firmware build date", app_date},
        {"Firmware build time", app_time},
    };

    // Добавление информации о прошивке в JSON
    for (size_t i = 0; i < sizeof(firmwarePairs) / sizeof(firmwarePairs[0]); i++)
    {
        cJSON_AddStringToObject(json, firmwarePairs[i].key, firmwarePairs[i].value);
    }

    // Получение локального времени
    char *tmp = _timestr("%d.%m.%Y %H:%M:%S", time(NULL), 32);
    if (tmp != NULL)
    {
        cJSON_AddStringToObject(json, "local_time", tmp);
        vPortFree(tmp);
    }

    // Остальная информация о статусе системы
    cJSON_AddStringToObject(json, "last_started", last_started);
    cJSON_AddNumberToObject(json, "working_time", working_time);
    cJSON_AddStringToObject(json, "last_ow_updated", last_ow_updated);
    cJSON_AddStringToObject(json, "sunrise_time", sunrise);
    cJSON_AddStringToObject(json, "sunset_time", sunset);
    cJSON_AddNumberToObject(json, "shade_on_sunrise", shade_sunrise);
    cJSON_AddNumberToObject(json, "shade_on_sunset", shade_sunset);
    cJSON_AddNumberToObject(json, "on_sunrise", move_on_sunrise);
    cJSON_AddNumberToObject(json, "on_sunset", move_on_sunset);
    cJSON_AddNumberToObject(json, "cal_status", calibrate);
    cJSON_AddNumberToObject(json, "length", length);
    cJSON_AddStringToObject(json, "move_status", move_status);
    cJSON_AddNumberToObject(json, "current_shade", shade);
    cJSON_AddNumberToObject(json, "current_pos", current_pos);
    cJSON_AddNumberToObject(json, "target_pos", target_pos);
    cJSON_AddNumberToObject(json, "free_heap", esp_heap_free_percent());

    str = cJSON_Print(json);
    cJSON_Delete(json);

    return str;
}

/**
 * @brief Создает JSON-строку с данными системы.
 *
 * Функция собирает информацию о системе, такую как SSID, пароль, локальный IP,
 * страну, город, временную зону, сервер времени, ключ O/W и токен Telegram,
 * а также другую информацию в формате JSON.
 *
 * @return Указатель на строку JSON. Должен быть освобожден вызовом free().
 *         Возвращает NULL в случае ошибки при создании JSON-объекта.
 */
static char *mqttSystemJson()
{
    char *str = NULL;

    // Массив, содержащий ключи и соответствующие значения
    struct
    {
        const char *key;   ///< Ключ для JSON-объекта
        const char *value; ///< Значение для JSON-объекта
    } pairs[] = {
        {"ssid", ssid},
        {"password", password},
        {"local ip", ip},
        {"country", country},
        {"city", city},
        {"timezone", timezone},
        {"time_server1", time_server1},
        {"time_server2", time_server2},
        {"ow_key", ow_key},
        {"tg_bot_token", tg_bot_token},
        {"last_system_updated", last_updated},
        {"update_url", update_url},
    };

    cJSON *json = cJSON_CreateObject();
    if (json == NULL)
    {
        return NULL; // Обработка ошибки при создании объекта JSON
    }

    // Добавление пар "ключ-значение" в JSON
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
    {
        cJSON_AddStringToObject(json, pairs[i].key, pairs[i].value);
    }

    cJSON_AddNumberToObject(json, "free_heap", esp_heap_free_percent());

    str = cJSON_Print(json);
    cJSON_Delete(json);

    return str;
}

/**
 * @brief Записывает 8-битное целочисленное значение в NVS.
 *
 * Эта функция открывает хранилище NVS, записывает указанное значение по заданному ключу
 * и закрывает хранилище.
 *
 * @param key Указатель на строку, представляющую ключ для хранения.
 * @param val 8-битное целочисленное значение для записи.
 * @return esp_err_t Коды ошибок ESP, указывающие на успешность операции.
 */
static esp_err_t nvs_write_u8(char *key, uint8_t val)
{
    nvs_handle_t handle;
    esp_err_t err;
    const char *tag = "save_uint8"; // Константная строка для логирования

    // Открытие NVS хранилища
    err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "nvs open success");
        ESP_LOGI(tag, "writing data (%d) to key (%s)", val, key);

        // Запись 8-битного значения
        err = nvs_set_u8(handle, key, val);
        if (err == ESP_OK)
        {
            // Коммит изменений
            err = nvs_commit(handle);
            if (err == ESP_OK)
            {
                ESP_LOGI(tag, "writing success");
            }
            else
            {
                ESP_LOGE(tag, "commit error (%s)", esp_err_to_name(err));
            }
        }
        else
        {
            ESP_LOGE(tag, "writing error (%s)", esp_err_to_name(err));
        }

        // Закрытие NVS хранилища
        nvs_close(handle);
    }
    else
    {
        ESP_LOGE(tag, "nvs open error (%s)", esp_err_to_name(err));
    }

    return err;
}

/**
 * @brief Записывает 16-битное целочисленное значение в NVS.
 *
 * Эта функция открывает хранилище NVS, записывает указанное 16-битное значение по заданному
 * ключу и закрывает хранилище. Если возникнут ошибки, они будут записаны в лог.
 *
 * @param key Указатель на строку, представляющую ключ для хранения.
 * @param val 16-битное целочисленное значение для записи.
 * @return esp_err_t Коды ошибок ESP, указывающие на успешность операции.
 */
static esp_err_t nvs_write_u16(char *key, uint16_t val)
{
    nvs_handle_t handle;
    esp_err_t err;
    const char *tag = "save_uint16"; // Константная строка для логирования

    // Открытие NVS хранилища
    err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "nvs open success");
        ESP_LOGI(tag, "writing data (%d) to key (%s)", val, key);

        // Запись 16-битного значения
        err = nvs_set_u16(handle, key, val);
        if (err == ESP_OK)
        {
            // Коммит изменений
            err = nvs_commit(handle);
            if (err == ESP_OK)
            {
                ESP_LOGI(tag, "writing success");
            }
            else
            {
                ESP_LOGE(tag, "commit error (%s)", esp_err_to_name(err));
            }
        }
        else
        {
            ESP_LOGE(tag, "writing error (%s)", esp_err_to_name(err));
        }

        // Закрытие NVS хранилища
        nvs_close(handle);
    }
    else
    {
        ESP_LOGE(tag, "nvs open error (%s)", esp_err_to_name(err));
    }

    return err;
}

/**
 * @brief Записывает строковое значение в NVS.
 *
 * Эта функция открывает хранилище NVS, записывает указанную строку по заданному ключу
 * и закрывает хранилище. Если возникнут ошибки, они будут записаны в лог.
 *
 * @param key Указатель на строку, представляющую ключ для хранения.
 * @param val Указатель на строку, представляющую значение для записи.
 * @return esp_err_t Коды ошибок ESP, указывающие на успешность операции.
 */
static esp_err_t nvs_write_str(const char *key, const char *val)
{
    nvs_handle_t handle;
    esp_err_t err;
    const char *tag = "save_nvs"; // Константная строка для логирования

    ESP_LOGI(tag, "Opening Non-Volatile Storage (NVS) handle... ");

    // Открытие NVS для записи
    err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(tag, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err; // Возврат ошибки, если не удалось открыть NVS
    }

    ESP_LOGI(tag, "NVS handle open success");
    ESP_LOGI(tag, "Writing data [%s] to key [%s] in NVS memory", val, key);

    // Запись строкового значения
    err = nvs_set_str(handle, key, val);
    if (err == ESP_OK)
    {
        // Коммит изменений
        err = nvs_commit(handle);
        if (err == ESP_OK)
        {
            ESP_LOGI(tag, "Writing success");
        }
        else
        {
            ESP_LOGE(tag, "Error committing changes (%s)", esp_err_to_name(err));
        }
    }
    else
    {
        ESP_LOGE(tag, "Writing error (%s)", esp_err_to_name(err));
    }

    // Закрытие NVS
    nvs_close(handle);

    return err; // Возврат результата операции
}

/**
 * @brief Подписывается на указанный MQTT-топик.
 *
 * Эта функция подписывается на заданный MQTT-топик с указанным уровнем качества обслуживания (QoS).
 * Возвращает true при успешной подписке, иначе возвращает false.
 *
 * @param client Указатель на MQTT-клиент.
 * @param topic Указатель на строку, представляющую топик для подписки.
 * @param qos Уровень качества обслуживания для подписки (0, 1 или 2).
 * @return true Если подписка выполнена успешно, иначе false.
 */
static bool mqttSubscribe(esp_mqtt_client_handle_t client, const char *topic, int qos)
{
    const char *tag = "mqttSubscribe";

    // Проверка аргументов на корректность
    if (client == NULL || topic == NULL)
    {
        ESP_LOGE(tag, "Invalid arguments: client or topic is NULL");
        return false;
    }

    // Подписка на указанное топик
    if (esp_mqtt_client_subscribe(client, topic, qos) != -1)
    {
        ESP_LOGI(tag, "Subscribed to topic %s with QoS %d", topic, qos);
        return true;
    }
    else
    {
        ESP_LOGE(tag, "Failed to subscribe to topic %s", topic);
        return false;
    }
}

/**
 * @brief Публикует сообщение в указанную MQTT тему.
 *
 * Эта функция отправляет сообщение с заданными данными в указанную тему,
 * используя предоставленный дескриптор MQTT-клиента. Функция логирует ошибку,
 * если какие-либо аргументы равны NULL или если публикация не удалась.
 *
 * @param client Дескриптор MQTT-клиента, используемого для публикации.
 * @param topic Тема, в которую будет опубликовано сообщение.
 * @param data Содержимое сообщения для публикации.
 * @param qos Уровень качества обслуживания для сообщения (0, 1 или 2).
 * @param retain Флаг удержания, указывающий, следует ли удерживать сообщение (0 или 1).
 * @return true Если сообщение успешно опубликовано, false в случае ошибок.
 */
static bool mqttPublish(esp_mqtt_client_handle_t client, char *topic, char *data, int qos, int retain)
{
    char *tag = "mqttPublish";

    // Проверка на NULL аргументы
    if (client == NULL || topic == NULL || data == NULL)
    {
        ESP_LOGE(tag, "NULL arguments");
        return false;
    }
    else
    {
        // Публикация сообщения в MQTT тему
        if (esp_mqtt_client_publish(client, topic, data, strlen(data), qos, retain) != -1)
        {
            ESP_LOGI(tag, "Publiched to topic %s", topic);
            return true;
        }
        else
        {
            ESP_LOGE(tag, "Failed to publish to topic %s", topic);
            return false;
        }
    }
}

/**
 * @brief Задача для периодической публикации статуса в MQTT.
 *
 * Эта функция выполняется в виде задачи FreeRTOS и предназначена для
 * периодической публикации статуса в определённую MQTT тему. Она проверяет
 * подключение к MQTT по флагу `mqttConnected`, генерирует статус в формате JSON
 * и вызывает функцию `mqttPublish`, чтобы отправить сообщение.
 * После публикации выделенная память для строки освобождается.
 *
 * @param params Указатель на параметры задачи (не используется).
 */
static void publish_task(void *params)
{
    const char *tag = "publish_task";

    while (true)
    {
        // Проверка на подключение к MQTT
        if (mqttConnected)
        {
            // Генерация статуса в формате JSON
            char *str = mqttStatusJson();

            // Публикация статуса в MQTT
            mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);

            // Освобождение памяти, если строка не NULL
            if (str != NULL)
                vPortFree(str);
        }

        // Задержка перед следующей публикацией (60 секунд)
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

/**
 * @brief Инициализация и запуск MQTT клиента.
 *
 * Эта функция отвечает за инициализацию MQTT клиента. Она получает MAC-адрес устройства,
 * формирует уникальные темы на основе MAC-адреса и настраивает параметры подключения
 * к брокеру MQTT, включая учетные данные и настройки "последней воли".
 * Если инициализация проходит успешно, MQTT клиент запускается.
 */
static void mqtt_start(void)
{
    char *tag = "mqtt_start";
    esp_err_t err;
    uint8_t mac[6];

    // Получаем MAC-адрес устройства
    err = esp_efuse_mac_get_default(mac);

    if (err == ESP_OK)
    {
        // Формирование уникального имени хоста и тем на основе MAC-адреса
        mqttHostname = _stringf("eShader-%x:%x:%x:%x:%x:%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        mqttTopicCheckOnline = _stringf("%s/checkonline", mqttHostname);
        mqttTopicControl = _stringf("%s/control", mqttHostname);
        mqttTopicStatus = _stringf("%s/status", mqttHostname);
        mqttTopicTimers = _stringf("%s/timers", mqttHostname);
        mqttTopicAddTimer = _stringf("%s/addtimer", mqttHostname);
        mqttTopicAddSunrise = _stringf("%s/addsunrise", mqttHostname);
        mqttTopicAddSunset = _stringf("%s/addsunset", mqttHostname);
        mqttTopicDelSunrise = _stringf("%s/delsunrise", mqttHostname);
        mqttTopicDelSunset = _stringf("%s/delsunset", mqttHostname);
        mqttTopicSystem = _stringf("%s/system", mqttHostname);
        mqttTopicSystemUpdate = _stringf("%s/system/update", mqttHostname);
        mqttTopicSystemTGKey = _stringf("%s/system/tgkey", mqttHostname);
        mqttTopicSystemOWKey = _stringf("%s/system/owkey", mqttHostname);
        mqttTopicSystemServerTime1 = _stringf("%s/system/servertime1", mqttHostname);
        mqttTopicSystemServerTime2 = _stringf("%s/system/servertime2", mqttHostname);
        mqttTopicSystemTimeZone = _stringf("%s/system/timezone", mqttHostname);
        mqttTopicSystemCountry = _stringf("%s/system/country", mqttHostname);
        mqttTopicSystemCity = _stringf("%s/system/city", mqttHostname);

        // Настройка параметров MQTT клиента
        esp_mqtt_client_config_t *mqtt_cfg;
        mqtt_cfg = (esp_mqtt_client_config_t *)calloc(1, sizeof(esp_mqtt_client_config_t));
        mqtt_cfg->broker.address.uri = mqttsServer;
        mqtt_cfg->broker.address.port = mqttTlsPort;
        mqtt_cfg->credentials.authentication.password = mqttPass;
        mqtt_cfg->credentials.username = mqttUser;
        mqtt_cfg->credentials.client_id = mqttHostname;
        mqtt_cfg->session.last_will.topic = mqttTopicCheckOnline;
        mqtt_cfg->session.last_will.msg = "offline";
        mqtt_cfg->session.last_will.qos = 1;
        mqtt_cfg->session.last_will.retain = 1;
        mqtt_cfg->session.last_will.msg_len = strlen("offline");
        mqtt_cfg->broker.verification.certificate = (const char *)wqtt_pem_start;

        // Инициализация MQTT клиента
        mqttClient = esp_mqtt_client_init(mqtt_cfg);
        if (mqttClient != NULL)
        {
            // Регистрация обработчика событий и запуск клиента
            esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, mqttClient);
            esp_mqtt_client_start(mqttClient);

            ESP_LOGI(tag, "MQTT start. Hostname: %s", mqttHostname);
        }

        // Освобождение памяти, если конфигурация не NULL
        if (mqtt_cfg != NULL)
        {
            vPortFree(mqtt_cfg);
        }
    }
    else
    {
        ESP_LOGE(tag, "Ошибка получения MAC-адреса");
    }
}

/**
 * @brief Попытка повторного подключения к MQTT-брокеру.
 *
 * Эта функция проверяет текущее состояние подключения. Если клиент
 * уже подключен, возвращается true. В противном случае происходит
 * попытка подключения к брокеру. При успешном подключении флаг
 * `mqttConnected` устанавливается в true.
 *
 * @return true Если подключение успешно.
 * @return false Если подключение не удалось.
 */
bool mqtt_reconnect()
{
    // Проверьте, соединены ли вы уже
    if (mqttConnected)
    {
        ESP_LOGI("MQTT_RECONNECT", "Already connected to MQTT broker.");
        return true;
    }

    // Попробуйте подключиться к MQTT-брокеру
    esp_err_t ret = esp_mqtt_client_start(mqttClient);
    if (ret == ESP_OK)
    {
        mqttConnected = true; // Установите флаг соединения
        ESP_LOGI("MQTT_RECONNECT", "Successfully connected to MQTT broker.");
        return true;
    }
    else
    {
        // Логируем ошибку подключения
        ESP_LOGE("MQTT_RECONNECT", "Failed to start MQTT client: %s", esp_err_to_name(ret));
        return false;
    }
}


/* Функция обработчик сообщений MQTT */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    char *tag = "mqtt_event";
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(tag, "MQTT_EVENT_BEFORE_CONNECT");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(tag, "MQTT_EVENT_CONNECTED");

        mqttConnected = true;

        // Если программа дошла до этого момента, то подтверждаем валидность прошивки
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        {
            ESP_LOGI(tag, "Firmware is valid");
        }
        else
        {
            ESP_LOGE(tag, "Firmware is not valid. Restarting...");
            esp_restart();
        }

        if (event->client != NULL)
        {
            // Публикуем состояние и подписываемся на топики
            mqttSubscribe(event->client, mqttTopicCheckOnline, mqttTopicCheckOnlineQoS);
            mqttSubscribe(event->client, mqttTopicStatus, mqttTopicStatusQoS);
            mqttSubscribe(event->client, mqttTopicTimers, mqttTopicTimersQoS);
            mqttSubscribe(event->client, mqttTopicControl, mqttTopicControlQoS);
            mqttSubscribe(event->client, mqttTopicAddTimer, mqttTopicAddTimerQoS);
            mqttSubscribe(event->client, mqttTopicAddSunrise, mqttTopicAddSunriseQoS);
            mqttSubscribe(event->client, mqttTopicAddSunset, mqttTopicAddSunsetQoS);
            mqttSubscribe(event->client, mqttTopicDelSunrise, mqttTopicDelSunriseQoS);
            mqttSubscribe(event->client, mqttTopicDelSunset, mqttTopicDelSunsetQoS);
            mqttSubscribe(event->client, mqttTopicSystem, mqttTopicSystemQoS);
            mqttSubscribe(event->client, mqttTopicSystemUpdate, mqttTopicSystemUpdateQoS);
            mqttSubscribe(event->client, mqttTopicSystemMaxSteps, mqttTopicSystemMaxStepsQoS);
            mqttSubscribe(event->client, mqttTopicSystemTGKey, mqttTopicSystemTGKeyQoS);
            mqttSubscribe(event->client, mqttTopicSystemOWKey, mqttTopicSystemOWKeyQoS);
            mqttSubscribe(event->client, mqttTopicSystemServerTime1, mqttTopicSystemServerTime1QoS);
            mqttSubscribe(event->client, mqttTopicSystemServerTime2, mqttTopicSystemServerTime2QoS);
            mqttSubscribe(event->client, mqttTopicSystemTimeZone, mqttTopicSystemTimeZoneQoS);
            mqttSubscribe(event->client, mqttTopicSystemCity, mqttTopicSystemCityQoS);
            mqttSubscribe(event->client, mqttTopicSystemCountry, mqttTopicSystemCountryQoS);

            mqttPublish(event->client, mqttTopicCheckOnline, "online", mqttTopicCheckOnlineQoS, mqttTopicCheckOnlineRet);

            char *status = mqttStatusJson();
            mqttPublish(event->client, mqttTopicStatus, status, mqttTopicStatusQoS, mqttTopicStatusRet);
            vPortFree(status);

            char *system = mqttSystemJson();
            mqttPublish(event->client, mqttTopicSystem, system, mqttTopicSystemQoS, mqttTopicSystemRet);
            vPortFree(system);

            tgMessage = _stringf("%s\n\nЗатемнение:  %d%%\nСостояние: %s", mqttHostname, shade, move_status);
            send_telegram_message(tgMessage);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        mqttConnected = false;
        ESP_LOGI(tag, "MQTT_EVENT_DISCONNECTED");

        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(tag, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);

        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(tag, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(tag, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(tag, "MQTT_EVENT_DATA");

        if (event->topic_len > 0)
        {
            char *topic = _stringl(event->topic, event->topic_len);
            char *data = _stringl(event->data, event->data_len);

            printf("topic= %s\n", topic);
            printf("data= %s\n", data);

            // Топик запроса статуса устройства
            if (!strcmp(topic, mqttTopicStatus))
            {
                if (!strcmp(data, "get"))
                {
                    ESP_LOGW(tag, "Get status topic received");

                    // Публикуем топик статуса
                    char *str = mqttStatusJson();
                    mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
                    vPortFree(str);
                }
            }

            // Топик добавления таймера при закате
            if (!strcmp(topic, mqttTopicAddSunrise))
            {
                move_on_sunrise = 1;
                shade_sunrise = strtol(data, NULL, 10);
                ESP_LOGW(tag, "Add sunrise topic received. Set shade on sunrise: %d", shade_sunrise);

                // Публикуем топик статуса
                char *str = mqttStatusJson();
                mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
                vPortFree(str);

                nvs_write_u8("shade_sunrise", shade_sunrise);
                nvs_write_u8("on_sunrise", move_on_sunrise);
            }

            // Топик добавления таймера при восходе
            if (!strcmp(topic, mqttTopicAddSunset))
            {
                move_on_sunset = 1;
                shade_sunset = strtol(data, NULL, 10);
                ESP_LOGW(tag, "Add sunset topic received. Set shade on sunset: %d", shade_sunset);

                // Публикуем топик статуса
                char *str = mqttStatusJson();
                mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
                vPortFree(str);

                nvs_write_u8("shade_sunset", shade_sunset);
                nvs_write_u8("on_sunset", move_on_sunset);
            }

            // Топик удаления таймера при закате
            if (!strcmp(topic, mqttTopicDelSunrise))
            {
                move_on_sunrise = 0;
                ESP_LOGW(tag, "Delete sunrise topic received");

                // Публикуем топик статуса
                char *str = mqttStatusJson();
                mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
                vPortFree(str);
                nvs_write_u8("on_sunrise", move_on_sunrise);
            }

            // Топик удаления таймера при восходе
            if (!strcmp(topic, mqttTopicDelSunset))
            {
                move_on_sunset = 0;
                ESP_LOGW(tag, "Delete sunset topic received");

                // Публикуем топик статуса
                char *str = mqttStatusJson();
                mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
                vPortFree(str);
                nvs_write_u8("on_sunset", move_on_sunset);
            }

            // Топик управления устройством
            if (!strcmp(topic, mqttTopicControl))
            {
                if (!strcmp(data, "calibrate"))
                {
                    onCalibrate();
                }
                else if (!strcmp(data, "stop"))
                {
                    onStop();
                }
                else
                {
                    shade = strtol(data, NULL, 10);
                    ESP_LOGI(tag, "Set shade: %d", shade);

                    if (shade < 0 || shade > 100)
                    {
                        ESP_LOGW(tag, "Invalid shade value: %d", shade);
                    }
                    else
                    {
                        if (calibrate == 1)
                        {
                            xTaskCreate(move_task, "move_task", 4096, NULL, 3, &move_task_handle);
                        }
                        else
                        {
                            ESP_LOGW(tag, "Shade is not calibrated");
                            move_status = _string("stopped");
                        }

                        // Публикуем топик статуса
                        char *str = mqttStatusJson();
                        mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
                        vPortFree(str);
                    }
                }
            }

            // Топик управления чтения системных параметров
            if (!strcmp(topic, mqttTopicSystem))
            {
                // Запрос на чтение параметров системы
                if (event->data_len > 0)
                {
                    if (!strcmp(data, "get"))
                    {
                        ESP_LOGW(tag, "Get system data topic received");

                        // Публикуем системный топик
                        char *str = mqttSystemJson();
                        mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                        vPortFree(str);
                    }

                    // Запрос на перезагрузку
                    else if (!strcmp(data, "reset"))
                    {
                        ESP_LOGW(tag, "Reset system data topic received");
                        esp_restart();
                    }

                    // Запрос на переинициализацию системы
                    else if (!strcmp(data, "erase"))
                    {
                        ESP_LOGW(tag, "Erase system data topic received");
                        xEventGroupClearBits(event_group, SC_START_BIT);
                        xEventGroupClearBits(event_group, WIFI_START_BIT);
                        xEventGroupSetBits(event_group, REINIT_BIT);

                        ESP_LOGI(tag, "System is reinitializing...");
                        ESP_ERROR_CHECK(nvs_flash_erase());
                        esp_err_t err = nvs_flash_init();
                        ESP_ERROR_CHECK(err);

                        vTaskDelay(pdMS_TO_TICKS(2000));
                        esp_restart();
                    }
                }
            }

            // Топик изменения ключа OpenWeatherMap api
            if (!strcmp(topic, mqttTopicSystemOWKey))
            {
                ESP_LOGW(tag, "Set new OpenWeather api key topic received: %s", data);

                if (event->data_len > 0)
                {
                    ow_key = _string(data);
                    // Сохраняем новое значение
                    nvs_write_str("ow_key", ow_key);

                    // Публикуем системный топик
                    char *str = mqttSystemJson();
                    mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                    vPortFree(str);
                }
                else
                {
                    ESP_LOGE(tag, "data_len error");
                }
            }

            // Топик изменения ключа Telegram api
            if (!strcmp(topic, mqttTopicSystemTGKey))
            {
                ESP_LOGW(tag, "Set new Telegram api key topic received: %s", data);

                if (event->data_len > 0)
                {
                    tg_bot_token = _string(data);
                    // Сохраняем новое значение
                    nvs_write_str("tg_bot_token", tg_bot_token);

                    // Публикуем системный топик
                    char *str = mqttSystemJson();
                    mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                    vPortFree(str);
                }
                else
                {
                    ESP_LOGE(tag, "data_len error");
                }
            }

            // Топик обновления прошивки по ota
            if (!strcmp(topic, mqttTopicSystemUpdate))
            {
                ESP_LOGW(tag, "Firmware update topic received: %s", data);
                char *header = "https://";

                if (event->data_len > 0)
                {
                    // Если приняли сообщение last обновляемся по последнему сохраненному пути
                    if (!strcmp(data, "last"))
                    {
                        nvs_handle_t nvs_handle;
                        /* Пытаемся открыть NVS для чтения*/
                        esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
                        if (err == ESP_OK)
                        {
                            ESP_LOGI(tag, "NVS storage open success");
                            size_t size;
                            char *str = "";
                            // Читаем url последнего обновления системы
                            err = nvs_get_str(nvs_handle, "update_url", NULL, &size);
                            if (err == ESP_OK)
                            {
                                str = malloc(size);
                                err = nvs_get_str(nvs_handle, "update_url", str, &size);
                                update_url = _string(str);
                                ESP_LOGI(tag, "Last updade url reading success: %s", update_url);
                            }
                        }
                        else
                        {
                            update_url = _string(UPDATE_URL);
                            ESP_LOGW(tag, "Last update url reading error (%s). Set default url: %s", esp_err_to_name(err), update_url);
                        }
                        nvs_close(nvs_handle);
                    }
                    else
                    {
                        update_url = _string(data);
                    }

                    // Проверяем заголовок, если начинается с https:// то все норм
                    // иначе отправляем системный топик с invalid_url
                    if (!strncmp(update_url, header, strlen(header)))
                    {
                        // Публикуем системный топик
                        char *str = mqttSystemJson();
                        mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                        vPortFree(str);

                        // Все выключаем
                        xTimerStop(timer1_handle, 0);

                        // Запускаем обновление
                        xTaskCreate(&ota_task, "ota_task", 4096, NULL, 3, NULL);
                    }
                    else
                    {
                        ESP_LOGE(tag, "Invalid url");
                        update_url = _string("invalid_url");
                        //  Публикуем системный топик
                        char *str = mqttSystemJson();
                        mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                        vPortFree(str);
                    }
                }
                else
                {
                    ESP_LOGE(tag, "data_len error");
                }
            }

            // Топик обновления сервера 1 синхронизации времени
            if (!strcmp(topic, mqttTopicSystemServerTime1))
            {
                ESP_LOGW(tag, "Set new server time topic received: %s", data);
                if (event->data_len > 0)
                {
                    time_server1 = _string(data);

                    // Сохрапняем в nvs
                    nvs_write_str("server_time1", time_server1);

                    // Публикуем системный топик
                    char *str = mqttSystemJson();
                    mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                    vPortFree(str);
                }
                else
                {
                    ESP_LOGE(tag, "data_len error");
                }
            }

            // Топик обновления сервера 2 синхронизации времени
            if (!strcmp(topic, mqttTopicSystemServerTime2))
            {
                ESP_LOGW(tag, "Set new server time topic received: %s", data);
                if (event->data_len > 0)
                {
                    time_server2 = _string(data);

                    // Сохраняем в nvs
                    nvs_write_str("server_time2", time_server2);

                    // Публикуем системный топик
                    char *str = mqttSystemJson();
                    mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                    vPortFree(str);
                }
                else
                {
                    ESP_LOGE(tag, "data_len error");
                }
            }

            // топик обновления временной зоны
            if (!strcmp(topic, mqttTopicSystemTimeZone))
            {
                ESP_LOGW(tag, "Set new timezone topic received: %s", data);

                if (event->data_len > 0)
                {
                    timezone = _string(data);

                    // Сохраняем в nvs
                    nvs_write_str("timezone", timezone);

                    // Публикуем системный топик
                    char *str = mqttSystemJson();
                    mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                    vPortFree(str);
                }
                else
                {
                    ESP_LOGE(tag, "data_len error");
                }
            }

            // Топик обновления города
            if (!strcmp(topic, mqttTopicSystemCity))
            {
                ESP_LOGW(tag, "Set new city topic received: %s", data);

                if (event->data_len > 0)
                {
                    city = _string(data);

                    // Сохраняем в nvs
                    nvs_write_str("city", city);

                    // Публикуем системный топик
                    char *str = mqttSystemJson();
                    mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                    vPortFree(str);
                }
                else
                {
                    ESP_LOGE(tag, "data_len error");
                }
            }

            // Топик обновления страны
            if (!strcmp(topic, mqttTopicSystemCountry))
            {
                ESP_LOGW(tag, "Set new country topic received: %s", data);
                if (event->data_len > 0)
                {
                    country = _string(data);

                    // Сохраняем в nvs
                    nvs_write_str("country", country);

                    // Публикуем системный топик
                    char *str = mqttSystemJson();
                    mqttPublish(event->client, mqttTopicSystem, str, mqttTopicSystemQoS, mqttTopicSystemRet);
                    vPortFree(str);
                }
                else
                {
                    ESP_LOGE(tag, "data_len error");
                }
            }
            if (!strcmp(topic, mqttTopicCheckOnline))
            {
                ESP_LOGW(tag, "Checkonline topic received");
                // Запрос на чтение параметров системы
                if (event->data_len > 0)
                {
                    if (!strcmp(data, "check"))
                    {
                        mqttPublish(event->client, mqttTopicCheckOnline, "online", mqttTopicCheckOnlineQoS, mqttTopicCheckOnlineRet);
                    }
                }
            }

            vPortFree(topic);
            vPortFree(data);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(tag, "MQTT_EVENT_ERROR");

        switch (event->error_handle->error_type)
        {
        case MQTT_ERROR_TYPE_TCP_TRANSPORT:
            ESP_LOGI(tag, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(tag, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(tag, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
            break;

        case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
            ESP_LOGI(tag, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            break;

        default:
            ESP_LOGW(tag, "Unknown error type: 0x%x", event->error_handle->error_type);
            break;
        }

        /** @brief Логика повторного подключения к MQTT-брокеру.
         *  Коды ошибок MQTT логируются, после чего осуществляется
         *  попытка повторного подключения. После достижения максимального
         *  количества попыток, устройство будет перезагружено.
         */
        for (int attempt = 0; attempt < MAX_RECONNECT_ATTEMPTS; ++attempt)
        {
            ESP_LOGI(tag, "Attempting to reconnect... (%d/%d)", attempt + 1, MAX_RECONNECT_ATTEMPTS);

            // Попытка повторного подключения
            if (mqtt_reconnect()) // Предполагается, что эта функция возвращает true при успешном подключении
            {
                ESP_LOGI(tag, "Reconnected successfully");
                return; // Завершим обработку, если подключение успешно
            }

            // Если не удалось, ждем перед следующей попыткой
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS)); // Пауза перед следующей попыткой
        }

        // Если после всех попыток соединение не удалось, перезагружаем устройство
        ESP_LOGE(tag, "Failed to reconnect after %d attempts. Restarting...", MAX_RECONNECT_ATTEMPTS);
        esp_restart();
        break;

    default:
        ESP_LOGW(tag, "Other event id:%d", event->event_id);
        break;
    }
}

// Функция обработчик событий smartconfig
static void sc_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *tag = "sc_event_handler";
    esp_err_t err;

    if (event_base == SC_EVENT)
    {
        switch (event_id)
        {
        /* smartconfig завершил сканирование точек доступа */
        case SC_EVENT_SCAN_DONE:
            ESP_LOGI(tag, "Smartconfig scan is done");
            break;

        /* smartconfig нашел канал целевой точки доступа */
        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI(tag, "Smartconfig found channel");
            xEventGroupSetBits(event_group, SC_FOUND_BIT);
            xEventGroupClearBits(event_group, SC_START_BIT);
            break;

        /* smartconfig получил имя сети SSID и пароль */
        case SC_EVENT_GOT_SSID_PSWD:
            smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

            uint8_t rvd_data[33] = {0};

            bzero(&wifi_config, sizeof(wifi_config_t));
            memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
            wifi_config.sta.bssid_set = evt->bssid_set;
            if (wifi_config.sta.bssid_set == true)
            {
                memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
            }

            memcpy(ssid, evt->ssid, sizeof(ssid));
            memcpy(password, evt->password, sizeof(password));
            nvs_write_str("ssid", ssid);
            nvs_write_str("pass", password);
            ESP_LOGI(tag, "Smartconfig got SSID and password. SSID: %s Pass: %s", ssid, password);
            ssid_loaded = true;
            password_loaded = true;

            if (evt->type == SC_TYPE_ESPTOUCH_V2)
            {
                ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
                ESP_LOGI(tag, "RVD_DATA:");
                for (int i = 0; i < 33; i++)
                {
                    printf("%02x ", rvd_data[i]);
                }
                printf("\n");
            }

            // Разрываем соединение
            err = esp_wifi_disconnect();
            if (err == ESP_OK)
            {
                ESP_LOGI(tag, "WiFi disconnect success");
            }
            else
            {
                ESP_LOGE(tag, "WiFi disconnect error: %s", esp_err_to_name(err));
            }

            xEventGroupClearBits(event_group, SC_START_BIT);
            xEventGroupClearBits(event_group, SC_FOUND_BIT);
            break;

        /* smartconfig отправил ACK на телефон */
        case SC_EVENT_SEND_ACK_DONE:
            xEventGroupSetBits(event_group, SC_DONE_BIT);
            xEventGroupClearBits(event_group, SC_START_BIT);
            xEventGroupClearBits(event_group, SC_FOUND_BIT);
            break;
        default:
            break;
        }
    }
}

/* Функция обработчик событий WiFi, IP, SC (SmartConfig) */
static void ota_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *tag = "ota_event_handler";

    if (event_base == ESP_HTTPS_OTA_EVENT)
    {
        switch (event_id)
        {
        case ESP_HTTPS_OTA_START:
            ESP_LOGI(tag, "OTA started");
            xEventGroupSetBits(event_group, OTA_START_BIT);
            break;
        case ESP_HTTPS_OTA_CONNECTED:
            ESP_LOGI(tag, "Connected to server");
            xEventGroupSetBits(event_group, OTA_CONNECT_BIT);
            xEventGroupClearBits(event_group, OTA_START_BIT);
            break;
        case ESP_HTTPS_OTA_GET_IMG_DESC:
            ESP_LOGI(tag, "Reading Image Description");
            break;
        case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
            ESP_LOGI(tag, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
            break;
        case ESP_HTTPS_OTA_DECRYPT_CB:
            ESP_LOGI(tag, "Callback to decrypt function");
            break;
        case ESP_HTTPS_OTA_WRITE_FLASH:
            ESP_LOGD(tag, "Writing to flash: %d written", *(int *)event_data);
            break;
        case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
            ESP_LOGI(tag, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
            break;
        case ESP_HTTPS_OTA_FINISH:
            ESP_LOGI(tag, "OTA finish");
            xEventGroupSetBits(event_group, OTA_FINISH_BIT);
            xEventGroupClearBits(event_group, OTA_START_BIT);
            xEventGroupClearBits(event_group, OTA_CONNECT_BIT);
            break;
        case ESP_HTTPS_OTA_ABORT:
            ESP_LOGE(tag, "OTA abort");
            esp_restart();
            break;
        default:
            break;
        }
    }
}

/* Функция обработчик событий HTTP */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    const char *tag = "http_event_handler";

    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        // Resize the buffer to fit the new chunk of data
        ESP_LOGI(tag, "HTTP_EVENT_ON_DATA message");

        ow_data = realloc(ow_data, ow_len + evt->data_len);
        memcpy(ow_data + ow_len, evt->data, evt->data_len);
        ow_len += evt->data_len;
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(tag, "HTTP_EVENT_ON_FINISH message");
        ESP_LOGI(tag, "OpenWeatherAPI received data: %s", ow_data);

        cJSON *str = cJSON_Parse(ow_data);
        cJSON *sys = cJSON_GetObjectItemCaseSensitive(str, "sys");

        // Читаем timezone, sunset, sunrise в UNIX формате
        sunrise_time = cJSON_GetObjectItemCaseSensitive(sys, "sunrise")->valueint;
        sunset_time = cJSON_GetObjectItemCaseSensitive(sys, "sunset")->valueint;

        if (sunrise != NULL)
            vPortFree(sunrise);
        sunrise = _timestr("%H:%M:%S", sunrise_time, 32);
        ESP_LOGI(tag, "Time sunrise: %s", sunrise);

        if (sunset != NULL)
            vPortFree(sunset);
        sunset = _timestr("%H:%M:%S", sunset_time, 32);
        ESP_LOGI(tag, "Time sunset: %s", sunset);

        if (last_ow_updated != NULL)
            vPortFree(last_ow_updated);
        last_ow_updated = _timestr("%d.%m.%Y %H:%M:%S", time(NULL), 32);

        ESP_LOGI(tag, "Last sunrise/sunset updated: %s", last_ow_updated);

        cJSON_Delete(str);
        vPortFree(ow_data);

        break;

    default:
        break;
    }

    return ESP_OK;
}

/* Задача запроса данных openweathermap */
static void openweather_task(void *param)
{
    const char *tag = "openweather_task";

    while (1)
    {
        if (time_sync)
        {
            char *url = _stringf("%s%s%s%s%s%s%s",
                                 "http://api.openweathermap.org/data/2.5/weather?q=",
                                 city,
                                 ",",
                                 country,
                                 "&units=metric",
                                 "&APPID=",
                                 ow_key);

            esp_http_client_config_t config = {
                .url = url,
                .method = HTTP_METHOD_GET,
                .event_handler = http_event_handler,
            };
            ESP_LOGI(tag, "Task started from url: %s", config.url);

            esp_http_client_handle_t client = esp_http_client_init(&config);

            esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK)
            {
                int status_code = esp_http_client_get_status_code(client);
                if (status_code == HttpStatus_Ok)
                {
                    ESP_LOGI(tag, "Status code success: %d", status_code);
                    ow_data = malloc(sizeof(char));
                    ow_len = 0;
                }
                else if (status_code == HttpStatus_Forbidden)
                {
                    ESP_LOGE(tag, "Failed to send message, too many messages, please wait");
                }
                else
                {
                    ESP_LOGE(tag, "Status code error: %d", status_code);
                }
            }
            else
            {
                ESP_LOGE(tag, "Perform %s Request Error: %s", config.url, esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);
            vPortFree(url);
        }
        vTaskDelay(pdMS_TO_TICKS(600000));
    }
}

/* Задача конфигурации с помощью SC SmartConfig*/
static void smartconfig_task(void *param)
{
    EventBits_t uxBits;
    esp_err_t err;
    const char *tag = "smartconfig_task";
    err = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "SC_TYPE_ESPTOUCH config success");
    }
    else
    {
        ESP_LOGE(tag, "SC_TYPE_ESPTOUCH config error");
    }

    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    err = esp_smartconfig_start(&cfg);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "Smartconfig start success");
    }
    else
    {
        ESP_LOGE(tag, "Smartconfig start error: %s", esp_err_to_name(err));
    }

    while (1)
    {
        uxBits = xEventGroupWaitBits(event_group, WIFI_DONE_BIT | SC_DONE_BIT, true, false, portMAX_DELAY);
        if (uxBits & WIFI_DONE_BIT)
        {
            ESP_LOGI(tag, "WiFi Connected to ap");
        }
        if (uxBits & SC_DONE_BIT)
        {
            ESP_LOGI(tag, "Smartconfig is done");
            err = esp_smartconfig_stop();
            if (err == ESP_OK)
            {
                ESP_LOGI(tag, "Smartconfig stop success");
            }
            else
            {
                ESP_LOGE(tag, "Smartconfig stop error: %s", esp_err_to_name(err));
            }
        }
    }
    vTaskDelete(NULL);
}

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    const char *tag = "validate_image_header";
    if (new_app_info == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK)
    {
        ESP_LOGI(tag, "Running firmware version: %s", running_app_info.version);
    }

    return ESP_OK;
}

static esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* Uncomment to add custom headers to HTTP request */
    // err = esp_http_client_set_header(http_client, "Custom-Header", "Value");
    return err;
}

/* Задача обновления через WiFi */
static void ota_task(void *param)
{
    const char *tag = "ota_task";
    esp_err_t ota_finish_err = ESP_OK;

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_http_client_config_t config = {
        .url = update_url,
        .use_global_ca_store = false,
        .crt_bundle_attach = esp_crt_bundle_attach,

    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .http_client_init_cb = _http_client_init_cb, // Register a callback to be invoked after esp_http_client is initialized
    };

    ESP_LOGI(tag, "Starting OTA firmware update from %s", config.url);

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(tag, "ESP HTTPS OTA Begin failed");
        esp_restart();
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK)
    {
        ESP_LOGE(tag, "esp_https_ota_read_img_desc failed");
        esp_restart();
    }

    err = validate_image_header(&app_desc);
    if (err != ESP_OK)
    {
        ESP_LOGE(tag, "image header verification failed");
        esp_restart();
    }

    while (1)
    {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
        {
            break;
        }
        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        ESP_LOGI(tag, "Image bytes read: %d", esp_https_ota_get_image_len_read(https_ota_handle));
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true)
    {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(tag, "Complete data was not received.");
    }
    else
    {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK))
        {
            ESP_LOGI(tag, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");

            // Получаем время последнего обновления и сохраняем в nvs
            last_updated = _timestr("%d.%m.%Y %H:%M:%S", time(NULL), 32);
            ESP_LOGI(tag, "Last updated: %s", last_updated);

            // Сохраняем новое значение
            nvs_write_str("last_updated", last_updated);

            // Сохраняем url обновления
            nvs_write_str("update_url", update_url);

            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
        else
        {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED)
            {
                ESP_LOGE(tag, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(tag, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
            vTaskDelete(NULL);
        }
    }
}

/* Управление вращением мотора */
static void move_task(void *param)
{
    char *tag = "sm_move_task";
    int dir = 0;

    target_pos = (int)(length * shade / 100.0);
    ESP_LOGI(tag, "Task started. Current position: %d Shade: %d New target: %d", current_pos, shade, target_pos);

    if (current_pos < target_pos)
    {
        // Направление движения - вниз (закрытие)
        move_status = _string("closing");
        gpio_set_level(SM_DIR, 1);
        // Разрешаем вращение
        gpio_set_level(SM_nEN, 0);
        dir = 1;
        ESP_LOGI(tag, "SM move started: (%s) to target: %d", move_status, target_pos);
    }
    if (current_pos > target_pos)
    {
        // Направление движения - вверх (открытие)
        move_status = _string("opening");
        gpio_set_level(SM_DIR, 0);
        // Разрешаем вращение
        gpio_set_level(SM_nEN, 0);
        dir = 2;
        ESP_LOGI(tag, "SM move started: (%s) to target: %d", move_status, target_pos);
    }
    if (current_pos == target_pos)
    {
        // Положение установлено
        move_status = _string("stopped");
        dir = 0;
        // Запрещаем вращение
        gpio_set_level(SM_nEN, 1);
        ESP_LOGI(tag, "SM on target: %d", target_pos);
    }

    tgMessage = _stringf("%s\n\nЗатемнение:  %d%%\nСостояние: %s", mqttHostname, shade, move_status);
    send_telegram_message(tgMessage);

    if (dir != 0)
    {
        // Сигналы вращения и индикации
        while (1)
        {
            if (dir == 1)
                current_pos++;
            if (dir == 2)
                current_pos--;
            if (current_pos <= 0)
                current_pos = 0;

            gpio_set_level(SM_STEP, 1);
            ets_delay_us(1000);
            gpio_set_level(SM_STEP, 0);
            ets_delay_us(1000);

            if (current_pos == target_pos)
                break;
        }

        // Снимаем сигнал разрешения
        gpio_set_level(SM_nEN, 1);
        ESP_LOGI(tag, "task stopped");
    }

    move_status = _string("stopped");
    nvs_write_u16("current_pos", current_pos);
    nvs_write_u16("target_pos", target_pos);

    // Публикуем топик статуса
    char *str = mqttStatusJson();
    mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
    vPortFree(str);

    tgMessage = _stringf("%s\n\nЗатемнение:  %d%%\nСостояние: %s", mqttHostname, shade, move_status);
    send_telegram_message(tgMessage);

    vTaskDelete(NULL);
}

/* Управление вращением мотора при калибровке */
static void calibrate_task(void *param)
{
    char *tag = "calibrate_task";
    calibrateCnt = 0;

    ESP_LOGI(tag, "Task started");

    // Задаем направление вниз и разрешаем вращение
    gpio_set_level(SM_nEN, 0);
    gpio_set_level(SM_DIR, 1);

    // Сигналы вращения
    while (1)
    {
        calibrateCnt++;

        gpio_set_level(SM_STEP, 1);
        ets_delay_us(1000);
        gpio_set_level(SM_STEP, 0);
        ets_delay_us(1000);
    }
    // Снимаем сигнал разрешения
    gpio_set_level(SM_nEN, 1);
    ESP_LOGE(tag, "Task stopped. System is not calibrated. Stepout: %d steps", calibrateCnt);
    calibrate = 0;
    move_status = _string("stopped");

    // Публикуем топик статуса
    char *str = mqttStatusJson();
    mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
    vPortFree(str);

    vTaskDelete(NULL);
}

// Задача светодиодной индикации режимов работы
static void led_task(void *param)
{
    EventBits_t uxBits;
    const char *tag = "led_task";
    ESP_LOGI(tag, "started...");

    while (1)
    {
        uxBits = xEventGroupGetBits(event_group);

        // Моргаем коротко по 1 разу при начале конфигурации smartconfig
        if ((uxBits & SC_START_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(25));
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        // Моргаем в 2 раза быстрее когда найден смартфон
        else if ((uxBits & SC_FOUND_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(25));
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        // Моргаем длинно пока подключаемся к сети
        else if ((uxBits & WIFI_START_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        // Моргаем коротко по 2 раза когда началось обновление
        else if ((uxBits & OTA_START_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(LED_STATUS, 0);

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        // Моргаем в 2 раза быстрее когда подключились к серверу обновлений
        else if ((uxBits & OTA_CONNECT_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(LED_STATUS, 0);

            vTaskDelay(pdMS_TO_TICKS(500));
        }
        // Зажигаем светодиод при режиме переинициализации или после обновления OTA
        else if (((uxBits & REINIT_BIT) != 0) || ((uxBits & OTA_FINISH_BIT) != 0))
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        else
        {
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    vTaskDelete(NULL);
}

// Задача опроса кнопки инициализации
static void init_btn_task(void *param)
{
    const char *tag = "init_btn_task";
    ESP_LOGI(tag, "started...");

    gpio_set_direction(SERVICE_BTN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SERVICE_BTN, GPIO_PULLUP_ONLY);

    int cnt = 0;
    while (1)
    {
        if (!gpio_get_level(SERVICE_BTN))
        {
            cnt++;
            ESP_LOGW(tag, "Button pressed: %d", cnt);

            // Когда досчитали до 50 (5 сек) очищаем память и сбрасываем
            if (cnt == 50)
            {
                xEventGroupClearBits(event_group, SC_START_BIT);
                xEventGroupClearBits(event_group, WIFI_START_BIT);
                xEventGroupSetBits(event_group, REINIT_BIT);

                ESP_LOGI(tag, "System is reinitializing...");
                ESP_ERROR_CHECK(nvs_flash_erase());
                esp_err_t err = nvs_flash_init();
                ESP_ERROR_CHECK(err);

                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            }
        }
        else
            cnt = 0;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

static void handler_on_wifi_disconnect(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *tag = "handler_on_wifi_disconnect";
    connect_retry++;
    if (connect_retry > max_connect_retry)
    {
        ESP_LOGI(tag, "WiFi Connect failed %d times, stop reconnect.", connect_retry);

        ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &handler_on_wifi_disconnect));
        ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &handler_on_sta_got_ip));
        ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &handler_on_wifi_connect));

        return;
    }
    ESP_LOGI(tag, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED)
    {
        return;
    }
    ESP_ERROR_CHECK(err);
}

static void handler_on_wifi_connect(void *esp_netif, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *tag = "handler_on_wifi_connect";
    ESP_LOGI(tag, "Connected to AP");

    // Запускаем синхронизацию времени
    time_sync_start("MSK-3");

    if (!ssid_loaded || !password_loaded)
    {
        xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 1, NULL);
        xEventGroupSetBits(event_group, SC_START_BIT);
    }
}

static void handler_on_sta_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *tag = "handler_on_sta_got_ip";
    connect_retry = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    ESP_LOGI(tag, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
}

/* Функция инциализации WiFi*/
static void wifi_init(void)
{
    char *tag = "wifi_init";
    ESP_LOGI(tag, "wifi initializating...");

    esp_netif_create_default_wifi_sta();

    /* Инициализируем WiFi значениями по умолчанию */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    connect_retry = 0;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &handler_on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handler_on_sta_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &handler_on_wifi_connect, NULL));

    // Переводим ESP в режим STA и запускаем WiFi
    memcpy(wifi_config.sta.password, password, password_size);
    memcpy(wifi_config.sta.ssid, ssid, ssid_size);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

/* Обработчик событий программного таймера c периодоим 1 секунда */
static void timer1_cb(TimerHandle_t pxTimer)
{
    const char *tag = "timer_1";

    working_time++;

    if (time_sync)
    {
        wating_to_time_sync = 0;

        // Получаем текущую дату и время и записываем в структуру статуса
        time_t now;
        time(&now);

        // Запоминаем время запуска
        if (!isStarted)
        {
            if (last_started != NULL)
                vPortFree(last_started);
            last_started = _timestr("%d.%m.%Y %H:%M:%S", time(NULL), 32);
            isStarted = true;
        }

        // Выводим данные в консоль
        char *str = _timestr("%d.%m.%Y %H:%M:%S", time(NULL), 32);
        printf("\rSystem is active. Time now %s Working time %lld sec. Free heap %0.1f %%", str, working_time, esp_heap_free_percent());
        fflush(stdout);
        vPortFree(str);

        // Запускаем MQTT
        if (!mqttConnected)
            mqtt_start();

        if (move_on_sunrise == 1 && now == sunrise_time)
        {
            shade = shade_sunrise;
            xTaskCreate(move_task, "move_task", 4096, NULL, 3, &move_task_handle);
        }
        if (move_on_sunset == 1 && now == sunset_time)
        {
            shade = shade_sunset;
            xTaskCreate(move_task, "move_task", 4096, NULL, 3, &move_task_handle);
        }
    }
    else
    {
        ESP_LOGW(tag, "Time is not synchronized");
        wating_to_time_sync++;
        if (wating_to_time_sync == DEFAULT_MAX_TIME_SYNC_WAITING)
        {
            //    esp_restart();
        }
    }
}

void app_main(void)
{
    char *tag = "main";

    event_group = xEventGroupCreate(); // Создаем группу событий
    // Инициализируем стек протоколов TCP/IP lwIP (Lightweight IP)
    ESP_ERROR_CHECK(esp_netif_init());
    // Создаем системный цикл событий
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &sc_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_handler, NULL));

    // Инициализация сигналов управления мотором и светодиодом на выход
    gpio_set_direction(SM_DIR, GPIO_MODE_OUTPUT);
    gpio_set_direction(SM_nEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SM_STEP, GPIO_MODE_OUTPUT);

    // Без подтяжки
    gpio_set_pull_mode(SM_DIR, GPIO_FLOATING);
    gpio_set_pull_mode(SM_nEN, GPIO_FLOATING);
    gpio_set_pull_mode(SM_STEP, GPIO_FLOATING);

    // Инициализация светодиода
    gpio_set_direction(LED_STATUS, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(LED_STATUS, GPIO_FLOATING);

    // Запрещаем вращение
    gpio_set_level(SM_nEN, 1);

    // Инициализируем NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t nvs_handle;
    /* Пытаемся открыть NVS для чтения*/
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "NVS storage open success");
        size_t size;
        char *str = "";
        /* Читаем ssid из NVS*/
        err = nvs_get_str(nvs_handle, "ssid", NULL, &ssid_size);
        if (err == ESP_OK)
        {
            ssid = malloc(ssid_size);
            err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_size);
            ESP_LOGI(tag, "SSID reading success: %s", ssid);
            ssid_loaded = true;
        }
        else
        {
            ESP_LOGW(tag, "SSID reading error (%s)", esp_err_to_name(err));
            ssid_loaded = false;
        }

        /* Читаем пароль из NVS */
        err = nvs_get_str(nvs_handle, "pass", NULL, &password_size);
        if (err == ESP_OK)
        {
            password = malloc(password_size);
            err = nvs_get_str(nvs_handle, "pass", password, &password_size);
            ESP_LOGI(tag, "Password reading success: %s", password);
            password_loaded = true;
        }
        else
        {
            ESP_LOGW(tag, "Password reading error (%s)", esp_err_to_name(err));
            password_loaded = false;
        }

        uint8_t data8 = 0;
        uint16_t data16 = 0;
        /* Читаем процент затемнения при восходе */
        err = nvs_get_u8(nvs_handle, "shade_sunset", &data8);
        if (err == ESP_OK)
        {
            shade_sunset = data8;
            ESP_LOGI(tag, "Shade sunset read success: %d", shade_sunset);
        }
        else
        {
            shade_sunset = 0;
            ESP_LOGW(tag, "Shade sunset read error (%s). Set default value: %d", esp_err_to_name(err), shade_sunset);
        }
        /* Читаем флаг при восходе */
        err = nvs_get_u8(nvs_handle, "on_sunset", &data8);
        if (err == ESP_OK)
        {
            move_on_sunset = data8;
            ESP_LOGI(tag, "Shade flag on sunset read success: %d", move_on_sunset);
        }
        else
        {
            move_on_sunset = 0;
            ESP_LOGW(tag, "Shade flag on sunset read error (%s). Set default value: %d", esp_err_to_name(err), move_on_sunset);
        }
        /* Читаем процент затемнения при закате */
        err = nvs_get_u8(nvs_handle, "shade_sunrise", &data8);
        if (err == ESP_OK)
        {
            shade_sunrise = data8;
            ESP_LOGI(tag, "Shade sunrise read success: %d", shade_sunrise);
        }
        else
        {
            shade_sunrise = 0;
            ESP_LOGW(tag, "Shade sunrise read error (%s). Set default value: %d", esp_err_to_name(err), shade_sunrise);
        }
        /* Читаем флаг при закате */
        err = nvs_get_u8(nvs_handle, "on_sunrise", &data8);
        if (err == ESP_OK)
        {
            move_on_sunrise = data8;
            ESP_LOGI(tag, "Shade flag on sunrise read success: %d", move_on_sunset);
        }
        else
        {
            move_on_sunrise = 0;
            ESP_LOGW(tag, "Shade flag on sunrise read error (%s). Set default value: %d", esp_err_to_name(err), move_on_sunrise);
        }
        /* Читаем статус калибровки */
        err = nvs_get_u8(nvs_handle, "cal_status", &data8);
        if (err == ESP_OK)
        {
            calibrate = data8;
            ESP_LOGI(tag, "Calibrate status read success: %d", calibrate);
        }
        else
        {
            calibrate = 0;
            ESP_LOGW(tag, "Calibrate status read error (%s). Set default value: %d", esp_err_to_name(err), calibrate);
        }
        /* Читаем длину шторы */
        err = nvs_get_u16(nvs_handle, "length", &data16);
        if (err == ESP_OK)
        {
            length = data16;
            ESP_LOGI(tag, "Shade length read success: %d", length);
        }
        else
        {
            length = 0;
            ESP_LOGW(tag, "Shade length read error (%s). Set default value: %d", esp_err_to_name(err), length);
        }
        /* Читаем последнее сохраненное текущее положение */
        err = nvs_get_u16(nvs_handle, "current_pos", &data16);
        if (err == ESP_OK)
        {
            current_pos = data16;
            ESP_LOGI(tag, "Current position read success: %d", current_pos);
        }
        else
        {
            current_pos = 0;
            ESP_LOGW(tag, "Current position read error (%s). Set default value: %d", esp_err_to_name(err), current_pos);
        }
        /* Читаем последнее сохраненное целевое положение */
        err = nvs_get_u16(nvs_handle, "target_pos", &data16);
        if (err == ESP_OK)
        {
            target_pos = data16;
            ESP_LOGI(tag, "Target position read success: %d", target_pos);
        }
        else
        {
            target_pos = 0;
            ESP_LOGW(tag, "Target position read error (%s). Set default value: %d", esp_err_to_name(err), target_pos);
        }
        // Читаем OpenWeatherMap api key
        err = nvs_get_str(nvs_handle, "ow_key", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "ow_key", str, &size);
            ow_key = _string(str);
            ESP_LOGI(tag, "Openweather api key reading success: %s", ow_key);
        }
        else
        {
            ow_key = _string(OPEN_WEATHER_MAP_TOKEN);
            ESP_LOGW(tag, "Openweather api key reading error (%s). Set default key: %s", esp_err_to_name(err), ow_key);
        }
        // Читаем Telegram api key
        err = nvs_get_str(nvs_handle, "tg_bot_token", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "tg_bot_token", str, &size);
            tg_bot_token = _string(str);
            ESP_LOGI(tag, "Telegram api key reading success: %s", tg_bot_token);
        }
        else
        {
            tg_bot_token = _string(TELEGRAM_BOT_TOKEN);
            ESP_LOGW(tag, "Telegram api key reading error (%s). Set default key: %s", esp_err_to_name(err), tg_bot_token);
        }
        // Читаем url сервера 1 синхронизации времени
        err = nvs_get_str(nvs_handle, "time_server1", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "time_server1", str, &size);
            time_server1 = _string(str);
            ESP_LOGI(tag, "Time server 1 reading success: %s", time_server1);
        }
        else
        {
            time_server1 = _string(TIME_SERVER1);
            ESP_LOGW(tag, "Time server 1 url reading error (%s). Set default url: %s", esp_err_to_name(err), time_server1);
        }
        // Читаем url сервера 2 синхронизации времени
        err = nvs_get_str(nvs_handle, "time_server2", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "time_server2", str, &size);
            time_server2 = _string(str);
            ESP_LOGI(tag, "Time server 2 reading success: %s", time_server2);
        }
        else
        {
            time_server2 = _string(TIME_SERVER2);
            ESP_LOGW(tag, "Time server 2 url reading error (%s). Set default url: %s", esp_err_to_name(err), time_server2);
        }
        // Читаем timezone
        err = nvs_get_str(nvs_handle, "timezone", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "timezone", str, &size);
            timezone = _string(str);
            ESP_LOGI(tag, "Timezone reading success: %s", time_server2);
        }
        else
        {
            timezone = _string(TZ);
            ESP_LOGW(tag, "Timezone reading error (%s). Set default tz: %s", esp_err_to_name(err), timezone);
        }
        // Читаем код страны
        err = nvs_get_str(nvs_handle, "country", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "country", str, &size);
            country = _string(str);
            ESP_LOGI(tag, "Country code reading success: %s", country);
        }
        else
        {
            country = _string(COUNTRY);
            ESP_LOGW(tag, "Country code reading error (%s). Set default country: %s", esp_err_to_name(err), country);
        }
        // Читаем код города
        err = nvs_get_str(nvs_handle, "city", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "city", str, &size);
            city = _string(str);
            ESP_LOGI(tag, "City code reading success: %s", city);
        }
        else
        {
            city = _string(CITY);
            ESP_LOGW(tag, "City code reading error (%s). Set default city: %s", esp_err_to_name(err), city);
        }
        // Читаем время последнего обновления системы
        err = nvs_get_str(nvs_handle, "last_updated", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "last_updated", str, &size);
            last_updated = _string(str);
            ESP_LOGI(tag, "Last updated time reading success: %s", last_updated);
        }
        else
        {
            last_updated = _string("no_updates");
            ESP_LOGW(tag, "Last updated time reading error (%s). Set default time: %s", esp_err_to_name(err), last_updated);
        }
        // Читаем url последнего обновления системы
        err = nvs_get_str(nvs_handle, "update_url", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "update_url", str, &size);
            update_url = _string(str);
            ESP_LOGI(tag, "Last updade url reading success: %s", update_url);
        }
        else
        {
            update_url = _string(UPDATE_URL);
            ESP_LOGW(tag, "Last update url reading error (%s). Set default url: %s", esp_err_to_name(err), update_url);
        }

        nvs_close(nvs_handle);
    }
    else
    {
        ESP_LOGE(tag, "NVS storage open error (%s)", esp_err_to_name(err));
        shade_sunrise = 0;
        shade_sunset = 0;
        move_on_sunrise = 0;
        move_on_sunset = 0;
    }

    // Читаем информацию о прошивке
    esp_app_desc_t *app_info = NULL;
    app_info = esp_app_get_description();

    app_name = _string(app_info->project_name);
    ESP_LOGI(tag, "Firmware name: %s", app_name);
    app_version = _string(app_info->version);
    ESP_LOGI(tag, "Firmware version: %s", app_version);
    app_date = _string(app_info->date);
    ESP_LOGI(tag, "Firmware date: %s", app_date);
    app_time = _string(app_info->time);
    ESP_LOGI(tag, "Firmware time: %s", app_time);

    move_status = _string("stopped");
    wifi_init();

    xTaskCreate(led_task, "led_task", 4096, NULL, 3, NULL);
    xTaskCreate(init_btn_task, "init_btn_task", 2048, NULL, 3, NULL);
    xTaskCreate(openweather_task, "openweather_task", 8000, NULL, 3, NULL);
    xTaskCreate(publish_task, "publish_task", 4096, NULL, 3, NULL);
    xTaskCreate(http_request_task, "http_request_task", 8192, NULL, 5, NULL);

    // Создаем программный таймер с периодом 1 секунда
    timer1_handle = xTimerCreate(
        "Timer_1s",
        pdMS_TO_TICKS(1000),
        pdTRUE,
        NULL,
        timer1_cb);

    // Запускаем таймер
    if (xTimerStart(timer1_handle, 0) == pdPASS)
    {
        ESP_LOGI(tag, "timer 1 started...");
    }

    // Запуск HTTP-сервера
    init_spiffs();
    start_webserver();
}
