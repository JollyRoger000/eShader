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

#define SM_DIR 19
#define SM_STEP 17
#define SM_nEN 18
#define SW 22
#define LED_STATUS 23
#define SERVICE_BTN 13

#define WEB_SERVER "https://api.telegram.org"
#define WEB_PORT "443"
#define WEB_URL "https://api.telegram.org/bot7001862513:AAEIJGOuRcs1qcXSK41S6RDdmtRsqbKh7TM/getme"

#define OW_KEY_DEFAULT "19fcdfb788eed5e53824116dc41ebe90"
#define TG_KEY_DEFAULT "7001862513:AAEIJGOuRcs1qcXSK41S6RDdmtRsqbKh7TM"

#define WIFI_START_BIT BIT0 // Бит запуска подключения к WiFi
#define WIFI_DONE_BIT BIT1  // Бит успешного подключения к WiFi
#define WIFI_FAIL_BIT BIT2  // Бит ошибки подключения (выставляется при ошибке подключения заданное число раз)
#define SC_START_BIT BIT3   // Бит запуска smartconfig
#define SC_DONE_BIT BIT4    // Бит успешнго завершения smartconfig
#define SC_FOUND_BIT BIT5   // Бит обнаружения смартфона SC
#define REINIT_BIT BIT6     // Бит переинициализации системы

char *openweather_data = NULL;
char mqttHostname[32];
const int mqttPort = 15476;
const char *mqttServer = "mqtt://m9.wqtt.ru";
const char *mqttUser = "u_3MLZE1";
const char *mqttPass = "78C0pl7e";

// const int mqttPort = 10528;
// const char *mqttServer = "mqtt://m5.wqtt.ru";
// const char *mqttUser = "u_6V43IR";
// const char *mqttPass = "S6F1CdP0";

char mqttTopicCheckOnline[50];
char mqttTopicControl[50];
char mqttTopicStatus[256];
char mqttTopicTimers[50];
char mqttTopicAddTimer[50];
char mqttTopicAddSunrise[50];
char mqttTopicAddSunset[50];
char mqttTopicDelSunrise[50];
char mqttTopicDelSunset[50];
char mqttTopicSystem[50];
char mqttTopicSystemUpdate[50];
char mqttTopicSystemMaxSteps[50];
char mqttTopicSystemTGKey[50];
char mqttTopicSystemOWKey[50];

char mqttStatusStr[200];

int mqttTopicStatusQoS = 0;
int mqttTopicCheckOnlineQoS = 0;
int mqttTopicControlQoS = 0;
int mqttTopicTimersQoS = 0;
int mqttTopicAddTimerQoS = 0;
int mqttTopicAddSunriseQoS = 0;
int mqttTopicAddSunsetQoS = 0;
int mqttTopicDelSunriseQoS = 0;
int mqttTopicDelSunsetQoS = 0;
int mqttTopicSystemQoS = 0;
int mqttTopicSystemUpdateQoS = 0;
int mqttTopicSystemMaxStepsQoS = 0;
int mqttTopicSystemTGKeyQoS = 0;
int mqttTopicSystemOWKeyQoS = 0;

int mqttTopicStatusRet = 1;
int mqttTopicCheckOnlinetRet = 1;
int mqttTopicControlRet = 1;
int mqttTopicTimersRet = 1;
int mqttTopicAddTimerRet = 1;
int mqttTopicAddSunriseRet = 1;
int mqttTopicAddSunsetRet = 1;
int mqttTopicDelSunriseRet = 1;
int mqttTopicDelSunsetRet = 1;
int mqttTopicSystemRet = 1;
int mqttTopicSystemUpdateRet = 1;
int mqttTopicSystemMaxStepsRet = 1;
int mqttTopicSystemTGKeyRet = 1;
int mqttTopicSystemOWKeyRet = 0;

typedef struct
{
    time_t sunrise; // В UNIX формате
    time_t sunset;  // В UNIX формате
    char str_sunrise[10];
    char str_sunset[10];
    char last_updated[20];
    uint8_t onSunrise;
    uint8_t onSunset;
    uint8_t shadeSunrise;
    uint8_t shadeSunset;
    char move_status[16];
    uint8_t shade;
    uint8_t cal_status;
    uint16_t target_pos;
    uint16_t current_pos;
    uint16_t length;
} StatusStruct;

StatusStruct _status = {
    .onSunrise = 0,
    .onSunset = 0,
    .shadeSunrise = 0,
    .shadeSunset = 0,
    .shade = 0,
    .move_status = "stopped",
    .cal_status = 0,
    .target_pos = 0,
    .current_pos = 0,
    .length = 0,
};

typedef struct
{
    uint16_t max_steps;
    char ow_key[50];
    char tg_key[100];
    char city[50];
    char country_code[5];
    char update_url[100];
} SystemStruct;

SystemStruct _system = {
    .max_steps = 30000,
    .ow_key = OW_KEY_DEFAULT,
    .city = "Moscow",
    .country_code = "RU",
    .tg_key = TG_KEY_DEFAULT,
    .update_url = "https://cs49635.tw1.ru/eShader/updates/eShader.bin",
};

EventGroupHandle_t event_group; // Группа событий
TaskHandle_t calibrate_task_handle;
TaskHandle_t move_task_handle;
TimerHandle_t _timer = NULL;
esp_mqtt_client_handle_t mqttClient;
wifi_config_t wifi_config; // Структура для хранения настроек WIFI

size_t openweather_len = 0;

int connect_retry = 0;
int max_connect_retry = 5;
int timer = 0;

uint16_t max_steps = 30000;
uint16_t calibrateCnt = 0;

char *moveStatus = "stopped";
bool sw_flag = false;
bool init_flag = false;
bool targetFlag = false;
bool ssid_loaded = false;
bool password_loaded = false;
bool time_sync = false;
bool mqttConnected = false;
bool owConnected = false;

void mqtt_start(void);
void time_sync_start(const char *tz);
void time_sync_cb(struct timeval *tv);
void timer_cb(TimerHandle_t pxTimer);
void onCalibrate();
void onStop();
void onShade(int shade);
void move_task(void *param);
void calibrate_task(void *param);
void smartconfig_task(void *param);
void openweather_task(void *param);
void wifi_connect_task(void *param);
void ota_task(void *param);
void led_task(void *param);
void init_btn_task(void *param);
char *mqttStatusJson(StatusStruct status);
char *mqttSystemJson(SystemStruct status);
bool get_sunrise_sunset(const char *json_string);
esp_err_t nvs_write_u8(char *key, uint8_t val);
esp_err_t nvs_write_u16(char *key, uint16_t val);
esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client);
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void sc_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void ota_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// Функция калибровки длины шторы
void onCalibrate()
{
    char *tag = "on_calibrate";
    ESP_LOGW(tag, "CALIBRATE message received");

    strcpy(_status.move_status, "calibrating");

    // Получаем строку статуса в json формате
    char *status = mqttStatusJson(_status);
    ESP_LOGI(tag, "New status string: %s", status);
    // Публикуем новый статус
    if (mqttConnected)
    {
        int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
        ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
    }
    free(status);
    // Запускаем задачу калибровки
    xTaskCreate(calibrate_task, "calibrate_task", 4096, NULL, 3, &calibrate_task_handle);
}

// Функция останова вращения
void onStop()
{
    char *tag = "on_stop";
    ESP_LOGW(tag, "STOP message received");

    // Если был запущен процесс калибровки, останавливаем и сохранаяем данные
    if (strcmp(_status.move_status, "calibrating") == 0)
    {
        vTaskSuspend(calibrate_task_handle);
        gpio_set_level(LED_STATUS, 0);
        _status.length = calibrateCnt;
        _status.cal_status = 1;
        strcpy(_status.move_status, "stopped");

        ESP_LOGI(tag, "Calibrate success. Shade lenght is: %d", _status.length);
        nvs_write_u16("length", _status.length);
        nvs_write_u8("cal_status", _status.cal_status);

        // Получаем строку статуса в json формате
        char *status = mqttStatusJson(_status);
        ESP_LOGI(tag, "New status string: %s", status);
        // Публикуем новый статус
        if (mqttConnected)
        {
            int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
            ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
        }
        free(status);
    }
    else if (!strcmp(_status.move_status, "opening") || !strcmp(_status.move_status, "closing"))
    {
        vTaskSuspend(move_task_handle);
        gpio_set_level(LED_STATUS, 0);
        strcpy(_status.move_status, "stopped");

        _status.target_pos = _status.current_pos;
        nvs_write_u16("current_pos", _status.current_pos);
        nvs_write_u16("target_pos", _status.target_pos);

        // Получаем строку статуса в json формате
        char *status = mqttStatusJson(_status);
        ESP_LOGI(tag, "New status string: %s", status);
        // Публикуем новый статус
        if (mqttConnected)
        {
            int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
            ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
        }
        free(status);
    }
}

// Функция перемещения в заданное положения
void onShade(int shade)
{
    char *tag = "on_shade";
    ESP_LOGI(tag, "New shade: (%d) message received", shade);

    _status.shade = shade;
    if (_status.cal_status == 1)
    {
        xTaskCreate(move_task, "move_task", 4096, NULL, 3, &move_task_handle);
    }
    else
    {
        ESP_LOGW(tag, "Shade is not calibrated");
        strcpy(_status.move_status, "stopped");
    }

    // Публикуем новый статус
    char *status = mqttStatusJson(_status);
    ESP_LOGI(tag, "New status string: %s", status);
    if (mqttConnected)
    {
        int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
        ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
    }
    free(status);
}

void time_sync_start(const char *tz)
{
    const char *tag = "time_sync_start";
    ESP_LOGI(tag, "started");
    // Выбираем часовой пояс и запускаем синхронизацию времени с SNTP
    setenv("TZ", tz, 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.nist.gov");
    sntp_set_time_sync_notification_cb(time_sync_cb);
    sntp_init();
}

/* Функция преобразования структуры статуса в json строку*/
char *mqttStatusJson(StatusStruct s)
{
    cJSON *json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "sunrise", s.str_sunrise);
    cJSON_AddStringToObject(json, "sunset", s.str_sunset);
    cJSON_AddStringToObject(json, "last_updated", s.last_updated);
    cJSON_AddNumberToObject(json, "shadeSunrise", s.shadeSunrise);
    cJSON_AddNumberToObject(json, "shadeSunset", s.shadeSunset);
    cJSON_AddNumberToObject(json, "onSunrise", s.onSunrise);
    cJSON_AddNumberToObject(json, "onSunset", s.onSunset);
    cJSON_AddNumberToObject(json, "cal_status", s.cal_status);
    cJSON_AddNumberToObject(json, "length", s.length);
    cJSON_AddNumberToObject(json, "shade", s.shade);
    cJSON_AddStringToObject(json, "move_status", s.move_status);
    cJSON_AddNumberToObject(json, "target_pos", s.target_pos);
    cJSON_AddNumberToObject(json, "current_pos", s.current_pos);

    char *string = cJSON_Print(json);

    cJSON_Delete(json);
    return string;
}

/* Функция преобразования структуры параметров системы в json строку*/
char *mqttSystemJson(SystemStruct s)
{
    cJSON *json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "country", s.country_code);
    cJSON_AddStringToObject(json, "city", s.city);
    cJSON_AddStringToObject(json, "ow_key", s.ow_key);
    cJSON_AddStringToObject(json, "tg_key", s.tg_key);
    cJSON_AddStringToObject(json, "update_url", s.update_url);
    cJSON_AddNumberToObject(json, "max_steps", s.max_steps);

    char *string = cJSON_Print(json);

    cJSON_Delete(json);
    return string;
}

/* Функция записи uint8 NVS */
esp_err_t nvs_write_u8(char *key, uint8_t val)
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
esp_err_t nvs_write_u16(char *key, uint16_t val)
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
esp_err_t nvs_write_str(char *key, char *val)
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

/* Функция обработчик событий HTTP */
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    const char *tag = "http_event_handler";
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        // Resize the buffer to fit the new chunk of data
        openweather_data = realloc(openweather_data, openweather_len + evt->data_len);
        memcpy(openweather_data + openweather_len, evt->data, evt->data_len);
        openweather_len += evt->data_len;
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(tag, "OpenWeatherAPI received data: %s", openweather_data);

        /* Выделяем из ответа время заката/восхода, преобразуем в JSON и публикуем */
        if (get_sunrise_sunset(openweather_data))
        {
            char *status = mqttStatusJson(_status);
            ESP_LOGI(tag, "New status string: %s", status);
            /* Публикуем новый статус */
            if (mqttConnected)
            {
                int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
                ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
            }
            free(status);
            free(openweather_data);
        }
        else
        {
            ESP_LOGE(tag, "get_sunrise_sunset error");
        }
        break;

    default:
        break;
    }
    return ESP_OK;
}

/* Функция обработчик сообщений MQTT */
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    char *tag = "mqtt_event";
    // ESP_LOGI(tag, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // mqttClient = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(tag, "MQTT_EVENT_BEFORE_CONNECT");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(tag, "MQTT_EVENT_CONNECTED");

        mqttConnected = true;
        // Публикуем состояние и подписываемся на топики
        msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicCheckOnline, "online", 0, mqttTopicCheckOnlineQoS, mqttTopicCheckOnlinetRet);
        ESP_LOGI(tag, "MQTT topic %s publish success, msg_id=%d", mqttTopicCheckOnline, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicAddSunrise, mqttTopicAddSunriseQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicAddSunrise, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicAddSunset, mqttTopicAddSunsetQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicAddSunset, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicAddTimer, mqttTopicAddTimerQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicAddTimer, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicStatus, mqttTopicStatusQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicStatus, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicTimers, mqttTopicTimersQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicTimers, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicControl, mqttTopicControlQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicControl, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicDelSunrise, mqttTopicDelSunriseQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicDelSunrise, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicDelSunset, mqttTopicDelSunsetQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicDelSunset, msg_id);

        // msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicSystem, mqttTopicSystemQoS);
        // ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicSystem, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicSystemOWKey, mqttTopicSystemOWKeyQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicSystemOWKey, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicSystemTGKey, mqttTopicSystemTGKeyQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicSystemTGKey, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicSystemMaxSteps, mqttTopicSystemMaxStepsQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicSystemMaxSteps, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicSystemUpdate, mqttTopicSystemUpdateQoS);
        ESP_LOGI(tag, "MQTT topic %s subscribe success, msg_id=%d", mqttTopicSystemUpdate, msg_id);

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

        char topic[50];
        char data[500];

        snprintf(topic, event->topic_len + 1, "%s", event->topic);
        snprintf(data, event->data_len + 1, "%s", event->data);

        printf("topic= %s\n", topic);
        printf("data= %s\n", data);

        // Если пришел запрос статуса устройства
        if (!strcmp(topic, mqttTopicStatus))
        {
            if (!strcmp(data, "get"))
            {
                ESP_LOGW(tag, "Get status topic received");

                char *status = mqttStatusJson(_status);
                ESP_LOGI(tag, "New status string: %s", status);
                // Публикуем cтатус
                if (mqttConnected)
                {
                    int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
                    ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
                }
                free(status);
            }
        }

        // Если пришел запрос на добавление таймера при закате
        if (!strcmp(topic, mqttTopicAddSunrise))
        {
            _status.onSunrise = 1;
            _status.shadeSunrise = atoi(data);
            ESP_LOGW(tag, "Add sunrise topic received. Set shade on sunrise: %d", _status.shadeSunrise);

            char *status = mqttStatusJson(_status);
            ESP_LOGI(tag, "New status string: %s", status);
            // Публикуем новый статус
            if (mqttConnected)
            {
                int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
                ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
            }
            free(status);

            nvs_write_u8("shade_sunrise", _status.shadeSunrise);
            nvs_write_u8("onSunrise", _status.onSunrise);
        }

        // Если пришел запрос на добавление таймера при восходе
        if (!strcmp(topic, mqttTopicAddSunset))
        {
            _status.onSunset = 1;
            _status.shadeSunset = atoi(data);
            ESP_LOGW(tag, "Add sunset topic received. Set shade on sunset: %d", _status.shadeSunset);

            char *status = mqttStatusJson(_status);
            ESP_LOGI(tag, "New status string: %s", status);
            // Публикуем новый статус
            if (mqttConnected)
            {
                int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
                ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
            }
            free(status);

            nvs_write_u8("shade_sunset", _status.shadeSunset);
            nvs_write_u8("onSunset", _status.onSunset);
        }

        // Если пришел запрос на удаление таймера при закате
        if (!strcmp(topic, mqttTopicDelSunrise))
        {
            _status.onSunrise = 0;
            ESP_LOGW(tag, "Delete sunrise topic received");

            char *status = mqttStatusJson(_status);
            ESP_LOGI(tag, "New status string: %s", status);
            // Публикуем новый статус
            if (mqttConnected)
            {
                int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
                ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
            }
            free(status);
            nvs_write_u8("onSunrise", _status.onSunrise);
        }

        // Если пришел запрос на удаление таймера при восходе
        if (!strcmp(topic, mqttTopicDelSunset))
        {
            _status.onSunset = 0;
            ESP_LOGW(tag, "Delete sunset topic received");

            char *status = mqttStatusJson(_status);
            ESP_LOGI(tag, "New status string: %s", status);
            // Публикуем новый статус
            if (mqttConnected)
            {
                int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
                ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
            }
            free(status);
            nvs_write_u8("onSunset", _status.onSunset);
        }

        // Если пришел запрос на выполнение команды управления
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
                onShade(atoi(data));
            }
        }

        // Если пришел запрос на чтение параметров устройства
        if (!strcmp(topic, mqttTopicSystem))
        {
            if (!strcmp(data, "get"))
            {
                ESP_LOGW(tag, "Get system data topic received");
                char *str = mqttSystemJson(_system);
                ESP_LOGI(tag, "System data: %s", str);
                if (mqttConnected)
                {
                    int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicSystem, str, 0, mqttTopicSystemQoS, mqttTopicSystemRet);
                    ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicSystem, msg_id, str);
                }
            }
        }

        // Если пришел запрос изменение параметра max_steps
        if (!strcmp(topic, mqttTopicSystemMaxSteps))
        {
            uint16_t val = strtol(data, NULL, 10);

            ESP_LOGW(tag, "Set new max_steps parameter topic received: %s", data);
            if (val > 0)
            {
                _system.max_steps = val;

                // Сохраняем новое значение
                nvs_write_u16("max_steps", _system.max_steps);

                // Преобразуем структуру в строку json и публикуем
                char *str = mqttSystemJson(_system);
                ESP_LOGI(tag, "System data: %s", str);
                if (mqttConnected)
                {
                    int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicSystem, str, 0, mqttTopicSystemQoS, mqttTopicSystemRet);
                    ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicSystem, msg_id, str);
                }
            }
            else
            {
                ESP_LOGE(tag, "New max_steps parameter failed");
            }
        }

        // Если пришел запрос на изменение ключа OpenWeatherMap api
        if (!strcmp(topic, mqttTopicSystemOWKey))
        {
            ESP_LOGW(tag, "Set new OpenWeather api key topic received: %s", data);

            strcpy(_system.ow_key, data);
            // Сохраняем новое значение
            nvs_write_str("ow_key", _system.ow_key);

            // Преобразуем структуру в строку json и публикуем
            char *str = mqttSystemJson(_system);
            ESP_LOGI(tag, "System data: %s", str);

            if (mqttConnected)
            {
                int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicSystem, str, 0, mqttTopicSystemQoS, mqttTopicSystemRet);
                ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicSystem, msg_id, str);
            }
        }

        // Если пришел запрос на изменение ключа Telegram api
        if (!strcmp(topic, mqttTopicSystemTGKey))
        {
            ESP_LOGW(tag, "Set new Telegram api key topic received: %s", data);

            strcpy(_system.tg_key, data);
            // Сохраняем новое значение
            nvs_write_str("tg_key", _system.tg_key);

            // Преобразуем структуру в строку json и публикуем
            char *str = mqttSystemJson(_system);
            ESP_LOGI(tag, "System data: %s", str);
            if (mqttConnected)
            {
                int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicSystem, str, 0, mqttTopicSystemQoS, mqttTopicSystemRet);
                ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicSystem, msg_id, str);
            }
        }

        // Если пришел запрос на обновление прошивки по ota
        if (!strcmp(topic, mqttTopicSystemUpdate))
        {
            ESP_LOGW(tag, "Firmware update topic received: %s", data);

            strcpy(_system.update_url, data);

            // Преобразуем структуру в строку json и публикуем
            char *str = mqttSystemJson(_system);
            ESP_LOGI(tag, "System data: %s", str);
            if (mqttConnected)
            {
                int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicSystem, str, 0, mqttTopicSystemQoS, mqttTopicSystemRet);
                ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicSystem, msg_id, str);
            }

            // Все выключаем
            xTimerStop(_timer, 0);
            // Запускаем обновление
            xTaskCreate(&ota_task, "ota_task", 4096, NULL, 3, NULL);
        }

        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE("mqtt_event", "MQTT_EVENT_ERROR");
        break;

    default:
        ESP_LOGW("mqtt_event", "Other event id:%d", event->event_id);
        break;
    }
}

// Функция обработчик собыьтий WiFi
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *tag = "wifi_event_handler";
    esp_err_t err;

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        /* Режим работы STA */
        case WIFI_EVENT_STA_START:
            ESP_LOGI(tag, "WIFI_EVENT_STA_START handle");

            /* Если ssid и pass прорчитаны из NVS запускаем задачу подключения к сети
             * в противном случае запускаем задачу конфигурации с помощью smart config */
            if (ssid_loaded && password_loaded)
            {
                xTaskCreate(wifi_connect_task, "wifi_connect_task", 4096, NULL, 1, NULL);
                xEventGroupSetBits(event_group, WIFI_START_BIT);
            }
            else
            {
                xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 1, NULL);
                xEventGroupSetBits(event_group, SC_START_BIT);
            }
            break;

        /* Соединение прервано */
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(tag, "WIFI_EVENT_STA_DISCONNECTED handle");
            ESP_LOGE(tag, "Connect to the AP fail");

            if (connect_retry < max_connect_retry)
            {
                ESP_LOGI(tag, "Retry to connect to the AP");
                err = esp_wifi_connect();
                if (err == ESP_OK)
                {
                    ESP_LOGI(tag, "esp_wifi_connect success");
                }
                {
                    ESP_LOGE(tag, "Failed to esp_wifi_connect, %d", err);
                }
                connect_retry++;
            }
            else
            {
                xEventGroupSetBits(event_group, WIFI_FAIL_BIT);
            }
            break;
        default:
            break;
        }
    }
}

// Функция обработчик событий IP
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *tag = "ip_event_handler";
    esp_err_t err;

    if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        /* Если подключение успешно и получен IP адрес*/
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(tag, "Connection success. IP addr: " IPSTR, IP2STR(&event->ip_info.ip));
            connect_retry = 0;
            xEventGroupSetBits(event_group, WIFI_DONE_BIT);
            xEventGroupClearBits(event_group, WIFI_START_BIT);

            break;

        default:
            break;
        }
    }
}

// Функция обработчик событий smartconfig
void sc_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
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

            char ssid[33] = {0};
            char password[65] = {0};
            uint8_t rvd_data[33] = {0};

            bzero(&wifi_config, sizeof(wifi_config_t));
            /* Копируем полученное имя сети в структуру wifi_config */
            memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
            /* Копируем полученный пароль в структуру wifi_config  */
            memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
            wifi_config.sta.bssid_set = evt->bssid_set;
            if (wifi_config.sta.bssid_set == true)
            {
                memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
            }

            memcpy(ssid, evt->ssid, sizeof(evt->ssid));
            memcpy(password, evt->password, sizeof(evt->password));
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

            //  Создаем новое подключение с принятыми данными
            err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            if (err == ESP_OK)
            {
                ESP_LOGI(tag, "WiFi set config success");
            }
            else
            {
                ESP_LOGE(tag, "WiFi set config error: %s", esp_err_to_name(err));
            }
            err = esp_wifi_connect();
            if (err == ESP_OK)
            {
                ESP_LOGI(tag, "WiFi connect success");
            }
            else
            {
                ESP_LOGE(tag, "WiFi connect error: %s", esp_err_to_name(err));
            }

            // esp_restart();
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
void ota_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *tag = "ota_event_handler";
    esp_err_t err;

    if (event_base == ESP_HTTPS_OTA_EVENT)
    {
        switch (event_id)
        {
        case ESP_HTTPS_OTA_START:
            ESP_LOGI(tag, "OTA started");
            break;
        case ESP_HTTPS_OTA_CONNECTED:
            ESP_LOGI(tag, "Connected to server");
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
            break;
        case ESP_HTTPS_OTA_ABORT:
            ESP_LOGI(tag, "OTA abort");
            break;
        default:
            break;
        }
    }
}

/* Задача запроса данных openweathermap */
void openweather_task(void *param)
{
    const char *tag = "openweather_task";
    char url[200];
    esp_http_client_handle_t client;
    int status_code = 0;

    while (1)
    {
        // Моргаем коротко 2 раза
        gpio_set_level(LED_STATUS, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LED_STATUS, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LED_STATUS, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LED_STATUS, 0);

        snprintf(url,
                 sizeof(url),
                 "%s%s%s%s%s%s%s",
                 "http://api.openweathermap.org/data/2.5/weather?q=",
                 _system.city,
                 ",",
                 _system.country_code,
                 "&units=metric",
                 "&APPID=",
                 _system.ow_key);

        ESP_LOGI(tag, "Task started from url: %s", url);

        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .event_handler = http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .is_async = true,
            .timeout_ms = 60000,
            // .cert_pem = (char *)server_cert_pem_start,

        };

        client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

        esp_http_client_perform(client);
        status_code = esp_http_client_get_status_code(client);
        if (status_code == 200)
        {
            ESP_LOGI(tag, "Perform OpenWeather API Request success. Status code: %d", status_code);
            break;
        }
        else
            ESP_LOGE(tag, "Perform OpenWeather API Request Error. Status code: %d", status_code);
        vTaskDelay(pdMS_TO_TICKS(10000));
    };

    gpio_set_level(LED_STATUS, 0);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

/* Выделяем из ответа openweather api данные о времени заката и восхода и записываем в структуру статуса */
bool get_sunrise_sunset(const char *json_string)
{
    char *tag = "get_ss_time";
    // Парсим JSON строку
    cJSON *str = cJSON_Parse(json_string);
    cJSON *sys = cJSON_GetObjectItemCaseSensitive(str, "sys");

    int cod = cJSON_GetObjectItemCaseSensitive(str, "cod")->valueint;

    if (cod == 200)
    {
        // Читаем timezone, sunset, sunrise в UNIX формате
        _status.sunrise = cJSON_GetObjectItemCaseSensitive(sys, "sunrise")->valueint;
        _status.sunset = cJSON_GetObjectItemCaseSensitive(sys, "sunset")->valueint;

        // Переводим из UNIX формата в читаемый
        struct tm *tm_sunrise;
        tm_sunrise = localtime(&_status.sunrise);
        strftime(_status.str_sunrise, sizeof(_status.str_sunrise), "%H:%M:%S", tm_sunrise);
        ESP_LOGI(tag, "Time sunrise: %s", _status.str_sunrise);

        struct tm *tm_sunset;
        tm_sunset = localtime(&_status.sunset);
        strftime(_status.str_sunset, sizeof(_status.str_sunset), "%H:%M:%S", tm_sunset);
        ESP_LOGI(tag, "Time sunset: %s", _status.str_sunset);

        struct tm *tm_now;
        time_t now = time(NULL);
        tm_now = localtime(&now);
        strftime(_status.last_updated, sizeof(_status.last_updated), "%d.%m.%Y %H:%M:%S", tm_now);
        ESP_LOGI(tag, "Last sunrise/sunset updated: %s", _status.last_updated);
        return true;
    }
    else
    {
        ESP_LOGE(tag, "Wrong OpenWeather API request code: %d", cod);
        return false;
    }
    cJSON_Delete(str);
}

/* Инициализация клиента MQTT */
void mqtt_start(void)
{
    char *tag = "mqtt_start";
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqttServer,
        .broker.address.port = mqttPort,
        .credentials.authentication.password = mqttPass,
        .credentials.username = mqttUser,
    };

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));

    sprintf(mqttHostname, "eShader-%x:%x:%x:%x:%x:%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    strcpy(mqttTopicCheckOnline, mqttHostname);
    strcat(mqttTopicCheckOnline, "/checkonline");
    strcpy(mqttTopicControl, mqttHostname);
    strcat(mqttTopicControl, "/control");
    strcpy(mqttTopicStatus, mqttHostname);
    strcat(mqttTopicStatus, "/status");
    strcpy(mqttTopicTimers, mqttHostname);
    strcat(mqttTopicTimers, "/timers");
    strcpy(mqttTopicAddTimer, mqttHostname);
    strcat(mqttTopicAddTimer, "/addtimer");
    strcpy(mqttTopicAddSunrise, mqttHostname);
    strcat(mqttTopicAddSunrise, "/addsunrise");
    strcpy(mqttTopicAddSunset, mqttHostname);
    strcat(mqttTopicAddSunset, "/addsunset");
    strcpy(mqttTopicDelSunset, mqttHostname);
    strcat(mqttTopicDelSunset, "/delsunset");
    strcpy(mqttTopicDelSunrise, mqttHostname);
    strcpy(mqttTopicSystem, mqttHostname);
    strcat(mqttTopicSystem, "/system");
    strcat(mqttTopicDelSunrise, "/delsunrise");
    strcpy(mqttTopicSystemUpdate, mqttHostname);
    strcat(mqttTopicSystemUpdate, "/system/update");
    strcpy(mqttTopicSystemMaxSteps, mqttHostname);
    strcat(mqttTopicSystemMaxSteps, "/system/maxsteps");
    strcpy(mqttTopicSystemTGKey, mqttHostname);
    strcat(mqttTopicSystemTGKey, "/system/tgkey");
    strcpy(mqttTopicSystemOWKey, mqttHostname);
    strcat(mqttTopicSystemOWKey, "/system/owkey");

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqttClient);

    ESP_LOGI(tag, "MQTT start. Hostname: %s", mqttHostname);
}

/* Задача конфигурации с помощью SC SmartConfig*/
void smartconfig_task(void *param)
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
            ESP_LOGI("smartconfig_task", "WiFi Connected to ap");
        }
        if (uxBits & SC_DONE_BIT)
        {
            ESP_LOGI("smartconfig_task", "Smartconfig is done");
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

esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
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

esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* Uncomment to add custom headers to HTTP request */
    // err = esp_http_client_set_header(http_client, "Custom-Header", "Value");
    return err;
}

/* Задача обновления через WiFi */
void ota_task(void *param)
{
    const char *tag = "ota_task";
    esp_err_t ota_finish_err = ESP_OK;

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_http_client_config_t config = {
        .url = _system.update_url,
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
        vTaskDelete(NULL);
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK)
    {
        ESP_LOGE(tag, "esp_https_ota_read_img_desc failed");
        goto ota_end;
    }
    err = validate_image_header(&app_desc);
    if (err != ESP_OK)
    {
        ESP_LOGE(tag, "image header verification failed");
        goto ota_end;
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
        ESP_LOGD(tag, "Image bytes read: %d", esp_https_ota_get_image_len_read(https_ota_handle));
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
            vTaskDelay(1000 / portTICK_PERIOD_MS);
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

ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(tag, "ESP_HTTPS_OTA upgrade failed");
    vTaskDelete(NULL);
}

/* Задача подключения к WiFi */
void wifi_connect_task(void *param)
{
    esp_err_t err;
    EventBits_t uxBits;
    const char *tag = "wifi_connect_task";

    /* Конфигурируем esp данными структуры wifi_config и подключаемся к AP (формируется сообщение WIFI_EVENT_STA_START)*/
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "WiFi config set");
    }
    else
    {
        ESP_LOGE(tag, "WiFi config set error: %s", esp_err_to_name(err));
    }
    err = esp_wifi_connect();
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "WiFi connect");
    }
    else
    {
        ESP_LOGE(tag, "WiFi connect error: %s", esp_err_to_name(err));
    }

    while (1)
    {
        /* Ждем пока не установятся биты WIFI_CONNECTED_BIT или WIFI_FAIL_BIT
         * WIFI_FAIL_BIT устанавливается при повторении ошибки подключения заданное количество раз */
        uxBits = xEventGroupWaitBits(event_group, WIFI_DONE_BIT | WIFI_FAIL_BIT, true, false, portMAX_DELAY);
        if (uxBits & WIFI_DONE_BIT)
        {
            ESP_LOGI("wifi_connect_task", "Connected to ap SSID: %s password: %s", wifi_config.sta.ssid, wifi_config.sta.password);

            // Запускаем синхронизацию времени
            time_sync_start("MSK-3");
        }
        else if (uxBits & WIFI_FAIL_BIT)
        {
            ESP_LOGI("wifi_connect_task", "Failed to connect to SSID: %s, password: %s", wifi_config.sta.ssid, wifi_config.sta.password);
            esp_restart();
        }
    }
    vTaskDelete(NULL);
}

/* Коллбек синхронизации времени по SNTP*/
void time_sync_cb(struct timeval *tv)
{
    struct tm timeinfo;
    char strftime_buf[20];
    ESP_LOGI("sntp_time_sync", "Time synchronization callback");

    localtime_r(&tv->tv_sec, &timeinfo);
    if (timeinfo.tm_year < (1970 - 1900))
    {
        ESP_LOGE("sntp_time_sync", "Time synchronization failed!");
        time_sync = false;
    }
    else
    {
        strftime(strftime_buf, sizeof(strftime_buf), "%H:%M:%S %d.%m.%Y", &timeinfo);
        ESP_LOGI("sntp_time_sync", "Time synchronization completed, current time: %s", strftime_buf);

        // Запускаем программный таймер с периодом 1 секунда
        if (xTimerStart(_timer, 0) == pdPASS)
        {
            ESP_LOGI("sntp_time_sync", "Timer started...");
        }
        time_sync = true;

        // Получаем время восхода/заката из openweather api
        xTaskCreate(openweather_task, "openweather_api_task", 4096, NULL, 3, NULL);

        // Запускаем MQTT
        mqtt_start();
    };
}

/* Обработчик событий программного таймера */
void timer_cb(TimerHandle_t pxTimer)
{
    unsigned long now;
    struct tm *tm_now;

    timer++;

    if (time_sync)
    {
        now = time(NULL);
        tm_now = localtime(&now);
        ESP_LOGI("timer", "Time now: %lu %02d:%02d:%02d Current pos: %d Target pos: %d Length: %d",
                 now, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec, _status.current_pos, _status.target_pos, _status.length);
        if (tm_now->tm_hour == 0 && tm_now->tm_min == 0 && tm_now->tm_sec == 0)
        {
        }
        if (_status.onSunrise == 1 && now == _status.sunrise)
        {
            _status.shade = _status.shadeSunrise;
            xTaskCreate(move_task, "move_task", 4096, NULL, 3, &move_task_handle);
        }
        if (_status.onSunset == 1 && now == _status.sunset)
        {
            _status.shade = _status.shadeSunset;
            xTaskCreate(move_task, "move_task", 4096, NULL, 3, &move_task_handle);
        }
    }
    else
    {
        ESP_LOGW("timer", "Time is not synchronized");
    }
}

/* Управление вращением мотора */
void move_task(void *param)
{
    char *tag = "sm_move_task";
    int dir = 0;
    int i = 0;

    _status.target_pos = (int)(_status.length * _status.shade / 100.0);

    if (_status.current_pos < _status.target_pos)
    {
        // Направление движения - вниз (закрытие)
        strcpy(_status.move_status, "closing");
        gpio_set_level(SM_DIR, 1);
        // Разрешаем вращение
        gpio_set_level(SM_nEN, 0);
        dir = 1;
        ESP_LOGI(tag, "SM move started: (%s) to target: %d", _status.move_status, _status.target_pos);
    }
    if (_status.current_pos > _status.target_pos)
    {
        // Направление движения - вверх (открытие)
        strcpy(_status.move_status, "opening");
        gpio_set_level(SM_DIR, 0);
        // Разрешаем вращение
        gpio_set_level(SM_nEN, 0);
        dir = 2;
        ESP_LOGI(tag, "SM move started: (%s) to target: %d", _status.move_status, _status.target_pos);
    }
    if (_status.current_pos == _status.target_pos)
    {
        // Положение установлено
        strcpy(_status.move_status, "stopped");
        dir = 0;
        // Запрещаем вращение
        gpio_set_level(SM_nEN, 1);
        ESP_LOGI(tag, "SM on target: %d", _status.target_pos);
    }

    if (dir != 0)
    {
        // Сигналы вращения и индикации
        while (_status.current_pos < max_steps)
        {
            i++;
            if (dir == 1)
                _status.current_pos++;
            if (dir == 2)
                _status.current_pos--;
            if (_status.current_pos <= 0)
                _status.current_pos = 0;

            gpio_set_level(SM_STEP, 1);
            if (i == 50)
                gpio_set_level(LED_STATUS, 1);
            ets_delay_us(1000);
            gpio_set_level(SM_STEP, 0);
            if (i == 100)
            {
                gpio_set_level(LED_STATUS, 0);
                i = 0;
            }
            ets_delay_us(1000);

            if (_status.current_pos == _status.target_pos)
                break;
        }

        // Снимаем сигнал разрешения
        gpio_set_level(SM_nEN, 1);
        ESP_LOGI(tag, "task stopped");
    }

    strcpy(_status.move_status, "stopped");
    nvs_write_u16("current_pos", _status.current_pos);
    nvs_write_u16("target_pos", _status.target_pos);

    // Публикуем новый статус
    char *status = mqttStatusJson(_status);
    ESP_LOGI(tag, "New status string: %s", status);
    if (mqttConnected)
    {
        int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
        ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
    }
    free(status);

    vTaskDelete(NULL);
}

/* Управление вращением мотора при калибровке */
void calibrate_task(void *param)
{
    char *tag = "calibrate_task";
    calibrateCnt = 0;

    ESP_LOGI(tag, "Task started");

    // Задаем направление вниз и разрешаем вращение
    gpio_set_level(SM_nEN, 0);
    gpio_set_level(SM_DIR, 1);

    int i = 0;
    // Сигналы вращения
    while (calibrateCnt < max_steps)
    {
        i++;
        calibrateCnt++;

        gpio_set_level(SM_STEP, 1);
        if (i == 20)
            gpio_set_level(LED_STATUS, 1);
        ets_delay_us(1000);
        gpio_set_level(SM_STEP, 0);
        if (i == 40)
        {
            gpio_set_level(LED_STATUS, 0);
            i = 0;
        }
        ets_delay_us(1000);
    }
    // Снимаем сигнал разрешения
    gpio_set_level(SM_nEN, 1);
    ESP_LOGE(tag, "Task stopped. System is not calibrated. Stepout: %d steps", calibrateCnt);
    _status.cal_status = 0;
    strcpy(_status.move_status, "stopped");

    // Публикуем новый статус
    char *status = mqttStatusJson(_status);
    ESP_LOGI(tag, "New status string: %s", status);
    if (mqttConnected)
    {
        int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, status, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
        ESP_LOGI(tag, "MQTT topic (%s) publish success, msg_id: %d, data: %s", mqttTopicStatus, msg_id, status);
    }
    free(status);

    vTaskDelete(NULL);
}

// Задача светодиодной индикации режимов работы
void led_task(void *param)
{
    EventBits_t uxBits;
    const char *tag = "led_task";
    ESP_LOGI(tag, "started...");

    while (1)
    {
        uxBits = xEventGroupGetBits(event_group);

        if ((uxBits & SC_START_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(25));
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        else if ((uxBits & SC_FOUND_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(25));
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        else if ((uxBits & WIFI_START_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(LED_STATUS, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        else if ((uxBits & REINIT_BIT) != 0)
        {
            gpio_set_level(LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        else
            gpio_set_level(LED_STATUS, 0);
    }
    vTaskDelete(NULL);
}

// Задача опроса кнопки инициализации
void init_btn_task(void *param)
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

/* Функция инциализации WiFi*/
void wifi_init(void)
{
    char *tag = "wifi_init";
    esp_err_t err;

    ESP_LOGI(tag, "wifi initializating...");

    event_group = xEventGroupCreate(); // Создаем группу событий
    // Инициализируем стек протоколов TCP/IP lwIP (Lightweight IP)
    err = esp_netif_init();
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "lwIP stack initialized");
    }
    else
    {
        ESP_LOGE(tag, "lwIP stack initialization error: %s", esp_err_to_name(err));
    }
    // Создаем системный цикл событий
    err = esp_event_loop_create_default();
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "system event loop created");
    }
    else
    {
        ESP_LOGE(tag, "system event loop creation error: %s", esp_err_to_name(err));
    }

    esp_netif_create_default_wifi_sta();

    /* Инициализируем WiFi значениями по умолчанию */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "WiFi initialized");
    }
    else
    {
        ESP_LOGE(tag, "WiFi initialization error: %s", esp_err_to_name(err));
    }

    /* Регистрируем события в функции обработчике */
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "WiFi event handler registered");
    }
    else
    {
        ESP_LOGE(tag, "WiFi event handler registration error: %s", esp_err_to_name(err));
    }

    err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "IP event handler registered");
    }
    else
    {
        ESP_LOGE(tag, "IP event handler registration error: %s", esp_err_to_name(err));
    }

    err = esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &sc_event_handler, NULL);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "SC event handler registered");
    }
    else
    {
        ESP_LOGE(tag, "SC event handler registration error: %s", esp_err_to_name(err));
    }

    err = esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_handler, NULL);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "OTA event handler registered");
    }
    else
    {
        ESP_LOGE(tag, "OTA event handler registration error: %s", esp_err_to_name(err));
    }

    // Переводим ESP в режим STA и запускаем WiFi
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "WiFi mode set to STA");
    }
    else
    {
        ESP_LOGE(tag, "WiFi mode set to STA error: %s", esp_err_to_name(err));
    }
    err = esp_wifi_start();
    if (err == ESP_OK)
    {
        ESP_LOGI(tag, "WiFi started");
    }
    else
    {
        ESP_LOGE(tag, "WiFi start error: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    char *tag = "main";

    // event_group = xEventGroupCreate(); // Создаем группу событий
    // esp_netif_init();                  // Инициализируем стек протоколов TCP/IP lwIP (Lightweight IP)
    // esp_event_loop_create_default();   // Создаем системный цикл событий

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
        err = nvs_get_str(nvs_handle, "ssid", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "ssid", str, &size);
            memcpy(wifi_config.sta.ssid, str, size);
            ESP_LOGI(tag, "SSID reading success: %s", wifi_config.sta.ssid);
            ssid_loaded = true;
        }
        else
        {
            ESP_LOGW(tag, "SSID reading error (%s)", esp_err_to_name(err));
            ssid_loaded = false;
        }

        /* Читаем пароль из NVS */
        err = nvs_get_str(nvs_handle, "pass", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "pass", str, &size);
            memcpy(wifi_config.sta.password, str, size);
            ESP_LOGI(tag, "Password reading success: %s", wifi_config.sta.password);
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
            _status.shadeSunset = data8;
            ESP_LOGI(tag, "Shade sunset read success: %d", _status.shadeSunset);
        }
        else
        {
            _status.shadeSunset = 0;
            ESP_LOGW(tag, "Shade sunset read error (%s). Set default value: %d", esp_err_to_name(err), _status.shadeSunset);
        }

        /* Читаем процент затемнения при закате */
        err = nvs_get_u8(nvs_handle, "shade_sunrise", &data8);
        if (err == ESP_OK)
        {
            _status.shadeSunrise = data8;
            ESP_LOGI(tag, "Shade sunrise read success: %d", _status.shadeSunrise);
        }
        else
        {
            _status.shadeSunrise = 0;
            ESP_LOGW(tag, "Shade sunrise read error (%s). Set default value: %d", esp_err_to_name(err), _status.shadeSunrise);
        }
        /* Читаем статус калибровки */
        err = nvs_get_u8(nvs_handle, "cal_status", &data8);
        if (err == ESP_OK)
        {
            _status.cal_status = data8;
            ESP_LOGI(tag, "Calibrate status read success: %d", _status.cal_status);
        }
        else
        {
            _status.cal_status = 0;
            ESP_LOGW(tag, "Calibrate status read error (%s). Set default value: %d", esp_err_to_name(err), _status.shadeSunrise);
        }
        /* Читаем длину шторы */
        err = nvs_get_u16(nvs_handle, "length", &data16);
        if (err == ESP_OK)
        {
            _status.length = data16;
            ESP_LOGI(tag, "Shade length read success: %d", _status.length);
        }
        else
        {
            _status.length = 0;
            ESP_LOGW(tag, "Shade length read error (%s). Set default value: %d", esp_err_to_name(err), _status.length);
        }
        /* Читаем последнее сохраненное текущее положение */
        err = nvs_get_u16(nvs_handle, "current_pos", &data16);
        if (err == ESP_OK)
        {
            _status.current_pos = data16;
            ESP_LOGI(tag, "Current position read success: %d", _status.current_pos);
        }
        else
        {
            _status.current_pos = 0;
            ESP_LOGW(tag, "Current position read error (%s). Set default value: %d", esp_err_to_name(err), _status.current_pos);
        }
        /* Читаем последнее сохраненное целевое положение */
        err = nvs_get_u16(nvs_handle, "target_pos", &data16);
        if (err == ESP_OK)
        {
            _status.target_pos = data16;
            ESP_LOGI(tag, "Target position read success: %d", _status.target_pos);
        }
        else
        {
            _status.target_pos = 0;
            ESP_LOGW(tag, "Target position read error (%s). Set default value: %d", esp_err_to_name(err), _status.target_pos);
        }
        // Читаем параметр max_steps
        err = nvs_get_u16(nvs_handle, "max_steps", &data16);
        if (err == ESP_OK)
        {
            _system.max_steps = data16;
            ESP_LOGI(tag, "Max steps parameter read success: %d", _system.max_steps);
        }
        else
        {
            _system.max_steps = 0;
            ESP_LOGW(tag, "Max steps parameter read error (%s). Set default value: %d", esp_err_to_name(err), _system.max_steps);
        }
        // Читаем OpenWeatherMap api key
        err = nvs_get_str(nvs_handle, "ow_key", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "ow_key", str, &size);
            memcpy(_system.ow_key, str, size);
            ESP_LOGI(tag, "Openweather api key reading success: %s", _system.ow_key);
        }
        else
        {
            strcpy(_system.ow_key, OW_KEY_DEFAULT);
            ESP_LOGW(tag, "Openweather api key reading error (%s). Set default key: %s", esp_err_to_name(err), _system.ow_key);
        }
        // Читаем Telegram api key
        err = nvs_get_str(nvs_handle, "tg_key", NULL, &size);
        if (err == ESP_OK)
        {
            str = malloc(size);
            err = nvs_get_str(nvs_handle, "tg_key", str, &size);
            memcpy(_system.tg_key, str, size);
            ESP_LOGI(tag, "Telegram api key reading success: %s", _system.tg_key);
        }
        else
        {
            strcpy(_system.tg_key, TG_KEY_DEFAULT);
            ESP_LOGW(tag, "Telegram api key reading error (%s). Set default key: %s", esp_err_to_name(err), _system.tg_key);
        }

        nvs_close(nvs_handle);
    }
    else
    {
        ESP_LOGE(tag, "NVS storage open error (%s)", esp_err_to_name(err));
        _status.shadeSunrise = 0;
        _status.shadeSunset = 0;
        _status.onSunrise = 0;
        _status.onSunset = 0;
    }

    /*
    char ssid[32] = "mywifi";
    char pass[32] = "mypass123";
    memcpy(wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, pass, sizeof(wifi_config.sta.ssid));
    password_loaded = true;
    ssid_loaded = true;
    */

    // char ssid[32] = "YA31";
    // char pass[32] = "audia3o765km190rus";
    // memcpy(wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    // memcpy(wifi_config.sta.password, pass, sizeof(wifi_config.sta.ssid));
    // password_loaded = true;
    // ssid_loaded = true;

    wifi_init();

    xTaskCreate(led_task, "led_task", 2048, NULL, 3, NULL);
    xTaskCreate(init_btn_task, "init_btn_task", 2048, NULL, 3, NULL);

    // Создаем программный таймер с периодом 1 секунда
    _timer = xTimerCreate(
        "Timer",
        pdMS_TO_TICKS(1000),
        pdTRUE,
        NULL,
        timer_cb);
}
