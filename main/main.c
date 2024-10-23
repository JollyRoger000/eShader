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
char *tg_key = NULL;
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

#define INDEX_HTML_PATH "/spiffs/index.html"
static char index_html[4096];
static long long index_html_size = 0;

// Функция инициализации spiffs
static void init_spiffs(void)
{
    const char *tag = "init_web_page_buffer";

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
static void read_index_html()
{
    const char *tag = "read_index_html";

    memset((void *)index_html, 0, sizeof(index_html));

    // Читаем состояние файла index.html
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st))
    {
        ESP_LOGE(tag, "index.html is not found");
    }
    // Читаем содержимое index.html
    else
    {
        index_html_size = st.st_size;
        ESP_LOGI(tag, "index.html found, size: %lld byte", index_html_size);

        FILE *fp = fopen(INDEX_HTML_PATH, "r");
        if (fread(index_html, index_html_size, 1, fp) == 0)
        {
            ESP_LOGE(tag, "file read failed");
        }
        else
        {
            ESP_LOGI(tag, "index.html read success");
        }
        fclose(fp);
    }
}

esp_err_t send_web_page(httpd_req_t *req)
{
    const char *tag = "send_web_page";
    int response = httpd_resp_send(req, index_html, index_html_size);
    ESP_LOGI(tag, "Response: %d", response);
    return response;
}

esp_err_t get_req_handler(httpd_req_t *req)
{
    return send_web_page(req);
}

httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = get_req_handler,
    .user_ctx = NULL};

httpd_handle_t server_setup(void)
{
    const char *tag = "local server setup...";

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get);
        ESP_LOGI(tag, "Server started");
    }
    else
    {
        ESP_LOGE(tag, "Failed to start server");
    }

    return server;
}

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

char *_stringf(const char *format, ...)
{
    const char *tag = "_stringf";
    char *ret = NULL;
    if (format != NULL)
    {
        // get the list of arguments
        va_list args1, args2;
        va_start(args1, format);
        va_copy(args2, args1);
        // calculate length of resulting string
        int len = vsnprintf(NULL, 0, format, args1);
        va_end(args1);
        // allocate memory for string
        if (len > 0)
        {
            ret = (char *)malloc(len + 1);
            if (ret != NULL)
            {
                memset(ret, 0, len + 1);
                vsnprintf(ret, len + 1, format, args2);
            }
            else
            {
                ESP_LOGE(tag, "Failed to format string: out of memory!");
            };
        };
        va_end(args2);
    };
    return ret;
}

char *_stringl(const char *source, const uint32_t len)
{
    const char *tag = "_stringl";
    if (source)
    {
        char *ret = (char *)malloc(len + 1);
        if (ret == NULL)
        {
            ESP_LOGE(tag, "Failed to create string: out of memory!");
            return NULL;
        }
        memset(ret, 0, len + 1);
        strncpy(ret, source, len);
        return ret;
    };
    return NULL;
}

char *_timestr(const char *format, time_t value, int bufsize)
{
    struct tm timeinfo;
    localtime_r(&value, &timeinfo);
    char buffer[bufsize];
    memset(buffer, 0, sizeof(buffer));
    strftime(buffer, sizeof(buffer), format, &timeinfo);
    return _string(buffer);
}

uint16_t format_string(char *buffer, uint16_t buffer_size, const char *format, ...)
{
    const char *tag = "format_string";
    uint16_t ret = 0;
    if (buffer && format)
    {
        memset(buffer, 0, buffer_size);
        // get the list of arguments
        va_list args;
        va_start(args, format);
        uint16_t len = vsnprintf(NULL, 0, format, args);
        // format string
        if (len + 1 > buffer_size)
        {
            ret = -len;
            ESP_LOGE(tag, "Buffer %d bytes too small to hold formatted string, %d bytes needed", buffer_size, len + 1);
        };
        ret = vsnprintf(buffer, buffer_size, format, args);
        va_end(args);
    };
    return ret;
}

// Функция для отправки сообщения в Telegram
static esp_err_t send_telegram_message(char *msg)
{
    const char *tag = "send_telegram_message";

    static char buf[2048];
    static char buf_timestamp[20];
    static char path[512];

    time_t now = time(NULL);
    strftime(buf_timestamp, sizeof(buf_timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    uint16_t size = format_string(buf, 2048, "{\"chat_id\":%s,\"parse_mode\":\"HTML\",\"disable_notification\":%s,\"text\":\"%s\r\n\r\n<code>%s</code>\"}",
                                  TELEGRAM_CHAT_ID,
                                  "false",
                                  msg,
                                  buf_timestamp);

    sprintf(path, "https://api.telegram.org/bot%s/sendMessage", TELEGRAM_BOT_TOKEN);

    esp_http_client_config_t *cfg;
    cfg = (esp_http_client_config_t *)calloc(1, sizeof(esp_http_client_config_t));
    cfg->path = path;
    cfg->host = "api.telegram.org";
    cfg->method = HTTP_METHOD_POST;
    cfg->transport_type = HTTP_TRANSPORT_OVER_SSL;
    cfg->cert_pem = (char *)tg_org_pem_start;
    cfg->port = 443;
    esp_http_client_handle_t client = esp_http_client_init(cfg);

    vPortFree(cfg);

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
        ESP_LOGE(tag, "Server status code error : %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI(tag, "Message send success");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

static float esp_heap_free_percent()
{
    return 100.0 * ((float)heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / (float)heap_caps_get_total_size(MALLOC_CAP_DEFAULT));
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

static void time_sync_start(const char *tz)
{
    const char *tag = "time_sync_start";
    ESP_LOGI(tag, "started");
    // Выбираем часовой пояс и запускаем синхронизацию времени с SNTP
    setenv("TZ", timezone, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, time_server1);
    esp_sntp_setservername(1, time_server2);
    sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
}

/* Функция преобразования структуры статуса в json строку*/
static char *mqttStatusJson()
{
    char *str = NULL;

    cJSON *json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "Firmware name", app_name);
    cJSON_AddStringToObject(json, "Firmware version", app_version);
    cJSON_AddStringToObject(json, "Firmware build date", app_date);
    cJSON_AddStringToObject(json, "Firmware build time", app_time);

    char *tmp = _timestr("%d.%m.%Y %H:%M:%S", time(NULL), 32);
    cJSON_AddStringToObject(json, "local_time", tmp);
    vPortFree(tmp);
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

/* Функция преобразования структуры параметров системы в json строку*/
static char *mqttSystemJson()
{
    char *str = NULL;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ssid", ssid);
    cJSON_AddStringToObject(json, "password", password);
    cJSON_AddStringToObject(json, "local ip", ip);
    cJSON_AddStringToObject(json, "country", country);
    cJSON_AddStringToObject(json, "city", city);
    cJSON_AddStringToObject(json, "timezone", timezone);
    cJSON_AddStringToObject(json, "time_server1", time_server1);
    cJSON_AddStringToObject(json, "time_server2", time_server2);
    cJSON_AddStringToObject(json, "ow_key", ow_key);
    cJSON_AddStringToObject(json, "tg_key", tg_key);
    cJSON_AddStringToObject(json, "last_system_updated", last_updated);
    cJSON_AddStringToObject(json, "update_url", update_url);
    cJSON_AddNumberToObject(json, "free_heap", esp_heap_free_percent());

    str = cJSON_Print(json);
    cJSON_Delete(json);

    return str;
}

/* Функция записи uint8 NVS */
static esp_err_t nvs_write_u8(char *key, uint8_t val)
{
    nvs_handle_t handle;
    esp_err_t err;
    char *tag = "save_uint8";
    err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "nvs open success");
        ESP_LOGI(tag, "writing data (%d) to key (%s)", val, key);
        err = nvs_set_u8(handle, key, val);
        if (err == ESP_OK)
        {
            nvs_commit(handle);
            ESP_LOGI(tag, "writing success");
        }
        else
        {
            ESP_LOGE(tag, "writing error (%s)", esp_err_to_name(err));
        }
        nvs_close(handle);
    }
    else
    {
        ESP_LOGE(tag, "nvs open error (%s)", esp_err_to_name(err));
    }
    return err;
}

/* Функция записи uint16 NVS */
static esp_err_t nvs_write_u16(char *key, uint16_t val)
{
    nvs_handle_t handle;
    esp_err_t err;
    char *tag = "save_uint16";
    err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "nvs open success");
        ESP_LOGI(tag, "writing data (%d) to key (%s)", val, key);
        err = nvs_set_u16(handle, key, val);
        if (err == ESP_OK)
        {
            nvs_commit(handle);
            ESP_LOGI(tag, "writing success");
        }
        else
        {
            ESP_LOGE(tag, "writing error (%s)", esp_err_to_name(err));
        }
        nvs_close(handle);
    }
    else
    {
        ESP_LOGE(tag, "nvs open error (%s)", esp_err_to_name(err));
    }
    return err;
}

/* Функция записи char* NVS*/
static esp_err_t nvs_write_str(char *key, char *val)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    char *tag = "save_nvs";
    ESP_LOGI(tag, "Opening Non-Volatile Storage (NVS) handle... ");
    /* Открываем NVS для записи*/
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(tag, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(tag, "NVS handle open success");
        ESP_LOGI(tag, "Writing data [%s] to key [%s] in NVS memory", val, key);
        err = nvs_set_str(my_handle, key, val);
        if (err == ESP_OK)
        {
            nvs_commit(my_handle);
            ESP_LOGI(tag, "Writing success");
        }
        else
        {
            ESP_LOGE(tag, "Writing Error!");
        }

        nvs_close(my_handle);
    }
    return err;
}

// Функция подписки на топики
static bool mqttSubscribe(esp_mqtt_client_handle_t client, char *topic, int qos)
{
    char *tag = "mqttSubscribe";

    if (client == NULL || topic == NULL)
        return false;
    else
    {
        if (esp_mqtt_client_subscribe(client, topic, qos) != -1)
        {
            ESP_LOGI(tag, "Subscribed to topic %s", topic);
            return true;
        }
        else
        {
            ESP_LOGE(tag, "Failed to subscribe to topic %s", topic);
            return false;
        }
    }
}

// Функция публикации топика
static bool mqttPublish(esp_mqtt_client_handle_t client, char *topic, char *data, int qos, int retain)
{
    char *tag = "mqttPublish";

    if (client == NULL || topic == NULL || data == NULL)
    {
        ESP_LOGE(tag, "NULL arguments");
        return false;
    }

    else
    {
        if (esp_mqtt_client_publish(client, topic, data, strlen(data), qos, retain) != -1)
        {
            ESP_LOGI(tag, "Published to topic %s", topic);
            return true;
        }
        else
        {
            ESP_LOGE(tag, "Failed to publish to topic %s", topic);
            return false;
        }
    }
}

static void publish_task(void *params)
{
    const char *tag = "publish_task";
    while (true)
    {
        if (mqttConnected)
        {
            char *str = mqttStatusJson();
            mqttPublish(mqttClient, mqttTopicStatus, str, mqttTopicStatusQoS, mqttTopicStatusRet);
            if (str != NULL)
                vPortFree(str);
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
/* Инициализация клиента MQTT */
static void mqtt_start(void)
{
    char *tag = "mqtt_start";
    esp_err_t err;
    uint8_t mac[6];
    err = esp_efuse_mac_get_default(mac);

    if (err == ESP_OK)
    {
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

        mqttClient = esp_mqtt_client_init(mqtt_cfg);
        if (mqttClient != NULL)
        {
            esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, mqttClient);
            esp_mqtt_client_start(mqttClient);

            ESP_LOGI(tag, "MQTT start. Hostname: %s", mqttHostname);
        }
        if (mqtt_cfg != NULL)
        {
            vPortFree(mqtt_cfg);
        }
    }
    else
    {
        ESP_LOGE(tag, "get MAC address error");
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
                    tg_key = _string(data);
                    // Сохраняем новое значение
                    nvs_write_str("tg_key", tg_key);

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
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGI(tag, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(tag, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(tag, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            ESP_LOGI(tag, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        }
        else
        {
            ESP_LOGW(tag, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
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

/* Коллбек синхронизации времени по SNTP*/
static void time_sync_cb(struct timeval *tv)
{
    const char *tag = "time_sync_cb";
    ESP_LOGI(tag, "Time is set from custom code: %lld", tv->tv_sec);
    time_sync = true;
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
        err = nvs_get_str(nvs_handle, "tg_key", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "tg_key", str, &size);
            tg_key = _string(str);
            ESP_LOGI(tag, "Telegram api key reading success: %s", tg_key);
        }
        else
        {
            tg_key = _string(TELEGRAM_BOT_TOKEN);
            ESP_LOGW(tag, "Telegram api key reading error (%s). Set default key: %s", esp_err_to_name(err), tg_key);
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

    // Запускаем локальный веб сервер
    init_spiffs();
    read_index_html();
    server_setup();
}
