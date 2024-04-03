#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "mqtt_client.h"
#include "esp_sntp.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_mac.h"
#include <cJSON.h>

TimerHandle_t _timer = NULL;
// static const HTTP_RESPONSE_BUFFER_SIZE = 1024;
static const int ESP_MAX_RETRY = 5;
static const int WIFI_CONNECTED_BIT = BIT0; // Бит успешного подключения к сети
static const int WIFI_FAIL_BIT = BIT1;      // Бит ошибки подключения (выставляется при ошибке подключения ESP_MAX_RETRY раз)
static const int ESPTOUCH_DONE_BIT = BIT2;  // Бит успешной отправки команды ESPTOUCH_DONE на смартфон

// API key from OpenWeatherMap
char open_weather_map_api_key[] = "19fcdfb788eed5e53824116dc41ebe90";
char city[] = "Moscow";
char country_code[] = "RU";
char *openweather_data = NULL;
size_t openweather_len = 0;
time_t sunrise; // В UNIX формате
time_t sunset;  // В UNIX формате

esp_mqtt_client_handle_t mqttClient;

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
char mqttTopicStatus[50];
char mqttTopicTimers[50];
char mqttTopicAddTimer[50];
char mqttTopicAddSunrise[50];
char mqttTopicAddSunset[50];

int mqttTopicStatusQoS = 1;
int mqttTopicCheckOnlineQoS = 1;
int mqttTopicControlQoS = 1;
int mqttTopicTimersQoS = 1;
int mqttTopicAddTimerQoS = 1;
int mqttTopicAddSunriseQoS = 1;
int mqttTopicAddSunsetQoS = 1;

int mqttTopicStatusRet = 1;
int mqttTopicCheckOnlinetRet = 1;
int mqttTopicControlRet = 1;
int mqttTopicTimersRet = 1;
int mqttTopicAddTimerRet = 1;
int mqttTopicAddSunriseRet = 1;
int mqttTopicAddSunsetRet = 1;

typedef struct
{
    char str_sunrise[10];
    char str_sunset[10];
    char last_updated[20];
} StatusStruct;

StatusStruct _status;

static EventGroupHandle_t s_wifi_event_group; // Группа событий
wifi_config_t wifi_config;                    // Структура для хранения настроек WIFI

static int s_retry_num = 0;
bool ssid_loaded = false;
bool password_loaded = false;
bool time_sync = false;
bool openweather_received = false;

static void smartconfig_task(void *param);
static void wifi_connect_task(void *param);
static void ota_task(void *param);
StatusStruct get_sunrise_sunset(const char *json_string);
static void mqtt_start(void);
void time_sync_start(const char *tz);
void time_sync_cb(struct timeval *tv);
void timer_cb(TimerHandle_t pxTimer);

void time_sync_start(const char *tz)
{
    // Выбираем часовой пояс и запускаем синхронизацию времени с SNTP
    setenv("TZ", tz, 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.nist.gov");
    sntp_set_time_sync_notification_cb(time_sync_cb);
    sntp_init();
}

char *mqttStatusJson(StatusStruct status)
{
    cJSON *json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "sunrise", status.str_sunrise);
    cJSON_AddStringToObject(json, "sunset", status.str_sunset);
    cJSON_AddStringToObject(json, "last_updated", status.last_updated);

    char *string = cJSON_Print(json);
    cJSON_Delete(json);
    return string;
}

/* Функция обработчик событий HTTP */
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        // Resize the buffer to fit the new chunk of data
        openweather_data = realloc(openweather_data, openweather_len + evt->data_len);
        memcpy(openweather_data + openweather_len, evt->data, evt->data_len);
        openweather_len += evt->data_len;
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI("http_event_handler", "OpenWeatherAPI received data: %s", openweather_data);
        openweather_received = true;

        /* Выделяем из ответа время заката/восхода, преобразуем в JSON и публикуем */
        _status = get_sunrise_sunset(openweather_data);
        char *str = mqttStatusJson(_status);
        printf("mqtt status JSON string: %s\n", str);
        int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, str, 0, mqttTopicStatusQoS, mqttTopicStatusRet);
        ESP_LOGI("mqtt_event", "MQTT topic %s publish success, msg_id=%d", mqttTopicCheckOnline, msg_id);

        free(openweather_data);
        break;

    default:
        break;
    }
    return ESP_OK;
}

/* Функция обработчик сообщений MQTT */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // mqttClient = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI("mqtt_event", "MQTT_EVENT_BEFORE_CONNECT");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI("mqtt_event", "MQTT_EVENT_CONNECTED");
        // Публикуем состояние и подписываемся на топики
        msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicCheckOnline, "online", 0, mqttTopicCheckOnlineQoS, mqttTopicCheckOnlinetRet);
        ESP_LOGI("mqtt_event", "MQTT topic %s publish success, msg_id=%d", mqttTopicCheckOnline, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicAddSunrise, mqttTopicAddSunriseQoS);
        ESP_LOGI("mqtt_event", "MQTT topic %s subscribe success, msg_id=%d", mqttTopicAddSunrise, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicAddSunset, mqttTopicAddSunsetQoS);
        ESP_LOGI("mqtt_event", "MQTT topic %s subscribe success, msg_id=%d", mqttTopicAddSunset, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicAddTimer, mqttTopicAddTimerQoS);
        ESP_LOGI("mqtt_event", "MQTT topic %s subscribe success, msg_id=%d", mqttTopicAddTimer, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicStatus, mqttTopicStatusQoS);
        ESP_LOGI("mqtt_event", "MQTT topic %s subscribe success, msg_id=%d", mqttTopicStatus, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicTimers, mqttTopicTimersQoS);
        ESP_LOGI("mqtt_event", "MQTT topic %s subscribe success, msg_id=%d", mqttTopicTimers, msg_id);

        msg_id = esp_mqtt_client_subscribe(mqttClient, mqttTopicControl, mqttTopicControlQoS);
        ESP_LOGI("mqtt_event", "MQTT topic %s subscribe success, msg_id=%d", mqttTopicControl, msg_id);

        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI("mqtt_event", "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI("mqtt_event", "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI("mqtt_event", "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI("mqtt_event", "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI("mqtt_event", "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE("mqtt_event", "MQTT_EVENT_ERROR");
        break;

    default:
        ESP_LOGW("mqtt_event", "Other event id:%d", event->event_id);
        break;
    }
}

/* Функция обработчик событий WiFi, IP, SC (SmartConfig) */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // switch (event_id)
    // {
    // case WIFI_EVENT_STA_START:
    //     if (ssid_loaded && password_loaded)
    //     {
    //         xTaskCreate(wifi_connect_task, "wifi_connect_task", 4096, NULL, 3, NULL);
    //     }
    //     else
    //     {
    //         xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
    //     }
    //     break;
    // case WIFI_EVENT_STA_DISCONNECTED:
    //     if (s_retry_num < ESP_MAX_RETRY)
    //     {
    //         esp_wifi_connect();
    //         s_retry_num++;
    //         ESP_LOGI("wifi_event_handler", "Retry to connect to the AP");
    //     }
    //     else
    //     {
    //         xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    //     }
    //     ESP_LOGE("wifi_event_handler", "Connect to the AP fail");
    //     break;
    // case IP_EVENT_STA_GOT_IPIP_EVENT_STA_GOT_IP:

    //  default:
    //     break;
    // }
    /* Режим работы STA */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        /* Если ssid и pass прорчитаны из NVS запускаем задачу подключения к сети
         * в противном случае запускаем задачу конфигурации с помощью smart config */
        if (ssid_loaded && password_loaded)
        {
            xTaskCreate(wifi_connect_task, "wifi_connect_task", 4096, NULL, 3, NULL);
        }
        else
        {
            xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
        }
    }
    /* Соединение прервано */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < ESP_MAX_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI("wifi_event_handler", "Retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGE("wifi_event_handler", "Connect to the AP fail");
    }
    /* Если подключение успешно и получен IP адрес*/
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI("wifi_event_handler", "Connection success. IP addr: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Запускаем синхронизацию времени
        time_sync_start("MSK-3");
    }
    /* smartconfig завершил сканирование точек доступа */
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
    {
        ESP_LOGI("wifi_event_handler", "Smartconfig scan is done");
    }
    /* smartconfig нашел канал целевой точки доступа */
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
    {
        ESP_LOGI("wifi_event_handler", "Smartconfig find channel");
    }
    /* smartconfig получил имя сети SSID и пароль */
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
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
        ESP_LOGI("wifi_event_handler", "Smartconfig got SSID and password. SSID: %s Pass: %s", ssid, password);

        nvs_handle_t my_handle;
        esp_err_t err;
        /* Открываем NVS для записи*/
        ESP_LOGI("wifi_event_handler", "Opening Non-Volatile Storage (NVS) handle... ");
        err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE("wifi_event_handler", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI("wifi_event_handler", "NVC handle open success");
            ESP_LOGI("wifi_event_handler", "Saving data to NVC");
            /* Сохраняем данные в памяти */
            ESP_ERROR_CHECK(nvs_set_str(my_handle, "ssid", ssid));

            /* Коммитим изменения */
            ESP_LOGI("wifi_event_handler", "NVC commiting...");
            ESP_ERROR_CHECK(nvs_commit(my_handle));

            /* Закрываем указатель */
            nvs_close(my_handle);
        }
        if (evt->type == SC_TYPE_ESPTOUCH_V2)
        {
            ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
            ESP_LOGI("wifi_event_handler", "RVD_DATA:");
            for (int i = 0; i < 33; i++)
            {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        /* Создаем подключение с принятыми данными */
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();
    }
    /* smartconfig отправил ACK на телефон */
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

/* Задача запроса данных openweathermap */
void openweather_api_task(void *pvParameters)
{
    openweather_received = false;

    char open_weather_map_url[200];
    snprintf(open_weather_map_url,
             sizeof(open_weather_map_url),
             "%s%s%s%s%s%s%s",
             "http://api.openweathermap.org/data/2.5/weather?q=",
             city,
             ",",
             country_code,
             "&units=metric",
             "&APPID=",
             open_weather_map_api_key);

    esp_http_client_config_t config = {
        .url = open_weather_map_url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200)
        {
            ESP_LOGI("openweather_api", "Message send success. Status code: %d", status_code);
        }
        else
        {
            ESP_LOGE("openweather_api", "Message sent fail. Status code: %d", status_code);
        }
    }
    else
    {
        ESP_LOGE("openweather_api", "Error esp_http_client_perform");
    }
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

StatusStruct get_sunrise_sunset(const char *json_string)
{
    StatusStruct status;

    // Парсим JSON строку
    cJSON *str = cJSON_Parse(json_string);
    cJSON *sys = cJSON_GetObjectItemCaseSensitive(str, "sys");

    // Читаем timezone, sunset, sunrise в UNIX формате
    sunrise = cJSON_GetObjectItemCaseSensitive(sys, "sunrise")->valueint;
    sunset = cJSON_GetObjectItemCaseSensitive(sys, "sunset")->valueint;

    // Переводим из UNIX формата в читаемый
    struct tm *tm_sunrise;
    tm_sunrise = localtime(&sunrise);
    strftime(status.str_sunrise, sizeof(status.str_sunrise), "%H:%M:%S", tm_sunrise);
    ESP_LOGI("get_sunrise_sunset", "Time sunrise: %s", status.str_sunrise);

    struct tm *tm_sunset;
    tm_sunset = localtime(&sunset);
    strftime(status.str_sunset, sizeof(status.str_sunset), "%H:%M:%S", tm_sunset);
    ESP_LOGI("get_sunrise_sunset", "Time sunset: %s", status.str_sunset);

    struct tm *tm_now;
    time_t now = time(NULL);
    tm_now = localtime(&now);
    strftime(status.last_updated, sizeof(status.last_updated), "%d.%m.%Y %H:%M:%S", tm_now);
    ESP_LOGI("get_sunrise_sunset", "Last sunrise/sunset updated: %s", status.last_updated);

    cJSON_Delete(str);

    return status;
}

/* Инициализация клиента MQTT */
static void mqtt_start(void)
{
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

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqttClient);

    ESP_LOGI("mqtt_init", "MQTT start. Hostname: %s", mqttHostname);
}

/* Функция инциализации WiFi*/
static void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());                // Инициализируем стек протоколов TCP/IP lwIP (Lightweight IP)
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Создаем системный цикл событий
    esp_netif_create_default_wifi_sta();

    /* Инициализируем WiFi значениями по умолчанию */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Регистрируем события в функции обработчике */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    /* Переводим ESP в режим STA и запускаем WiFi*/
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* Задача конфигурации с помощью SC SmartConfig*/
static void smartconfig_task(void *param)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    while (1)
    {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if (uxBits & WIFI_CONNECTED_BIT)
        {
            ESP_LOGI("smartconfig_task", "WiFi Connected to ap");
        }
        if (uxBits & ESPTOUCH_DONE_BIT)
        {
            ESP_LOGI("smartconfig_task", "Smartconfig is done");
            ESP_ERROR_CHECK(esp_smartconfig_stop());
            vTaskDelete(NULL);
        }
    }
}

/* Задача обновления через WiFi */
static void ota_task(void *param)
{
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_http_client_config_t config = {
        .url = "https://cs49635.tw1.ru/simple_ota.bin",
        .use_global_ca_store = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    ESP_LOGI("ota_task", "Starting OTA firmware update from %s", config.url);

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK)
    {
        ESP_LOGI("ota_task", "OTA Succeed, Rebooting...");
        esp_restart();
    }
    else
    {
        ESP_LOGE("ota_task", "Firmware upgrade failed");
    }
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

/* Задача подключения к WiFi */
static void wifi_connect_task(void *param)
{
    EventBits_t uxBits;

    /* Конфигурируем esp данными структуры wifi_config и подключаемся к AP (формируется сообщение WIFI_EVENT_STA_START)*/
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    while (1)
    {
        /* Ждем пока не установятся биты WIFI_CONNECTED_BIT или WIFI_FAIL_BIT
         * WIFI_FAIL_BIT устанавливается при повторении ошибки подключения заданное количество раз */
        uxBits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, true, false, portMAX_DELAY);
        if (uxBits & WIFI_CONNECTED_BIT)
        {
            ESP_LOGI("wifi_connect_task", "Connected to ap SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
        }
        else if (uxBits & WIFI_FAIL_BIT)
        {
            ESP_LOGI("wifi_connect_task", "Failed to connect to SSID: %s, password: %s", wifi_config.sta.ssid, wifi_config.sta.password);
            esp_restart();
        }
        else
        {
            ESP_LOGE("wifi_connect_task", "UNEXPECTED EVENT");
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
        xTaskCreate(&openweather_api_task, "openweather_api_task", 4096, NULL, 3, NULL);

        // Запускаем MQTT
        mqtt_start();
    };
}

/* Обработчик событий программного таймера */
void timer_cb(TimerHandle_t pxTimer)
{
    unsigned long now;
    struct tm *tm_now;

    if (time_sync)
    {
        now = time(NULL);
        tm_now = localtime(&now);
        ESP_LOGI("timer", "Time now: %lu %02d:%02d:%02d", now, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
        if (tm_now->tm_hour == 0 && tm_now->tm_min == 0 && tm_now->tm_sec == 0)
        {
            xTaskCreate(&openweather_api_task, "openweather_api_task", 4096, NULL, 3, NULL);
        }

        if (openweather_received)
        {
        }

        // if (tm_now->tm_hour == 16 && tm_now->tm_min == 13 && tm_now->tm_sec == 0)
        // {
        //     xTaskCreate(&ota_task, "ota_task", 4096, NULL, 3, NULL);
        // }
    }
    else
    {
        ESP_LOGW("timer", "Time is not synchronized");
    }
}

void app_main(void)
{
    // Инициализируем NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Открываем NVS и читаем ssid и password
    ESP_LOGI("main", "Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t my_handle;
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("main", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ssid_loaded = false;
        password_loaded = false;
    }
    else
    {
        printf("Done\n");

        ssid_loaded = false;
        password_loaded = false;

        /* Читаем SSID из NVS */
        ESP_LOGI("main", "Reading ssid from NVS ... ");
        size_t nvs_required_size;
        err = nvs_get_str(my_handle, "ssid", NULL, &nvs_required_size);
        switch (err)
        {
        case ESP_OK:
            char *nvs_ret_data;
            nvs_ret_data = malloc(nvs_required_size);
            err = nvs_get_str(my_handle, "ssid", nvs_ret_data, &nvs_required_size);
            memcpy(wifi_config.sta.ssid, nvs_ret_data, nvs_required_size);
            ESP_LOGI("main", "SSID read success: %s", wifi_config.sta.ssid);
            free(nvs_ret_data);
            ssid_loaded = true;
            break;

        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGW("main", "SSID is not found");
            break;

        default:
            ESP_LOGE("main", "Error (%s) reading!", esp_err_to_name(err));
        }

        /* Читаем PASS из NVS */
        ESP_LOGI("main", "Reading password from NVS ... ");
        err = nvs_get_str(my_handle, "pass", NULL, &nvs_required_size);
        switch (err)
        {
        case ESP_OK:
            printf("Done\n");
            char *nvs_ret_data;
            nvs_ret_data = malloc(nvs_required_size);
            err = nvs_get_str(my_handle, "pass", nvs_ret_data, &nvs_required_size);
            memcpy(wifi_config.sta.password, nvs_ret_data, nvs_required_size);
            ESP_LOGI("main", "Password read success: %s", wifi_config.sta.password);
            free(nvs_ret_data);
            password_loaded = true;
            break;

        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGW("main", "Password is not found");
            break;

        default:
            ESP_LOGE("main", "Error (%s) reading!", esp_err_to_name(err));
        }
        // Close
        nvs_close(my_handle);
    }

    /*
    char ssid[32] = "mywifi";
    char pass[32] = "mypass123";
    memcpy(wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, pass, sizeof(wifi_config.sta.ssid));
    password_loaded = true;
    ssid_loaded = true;
    */

    wifi_init();

    // Создаем программный таймер с периодоим 1 секунда
    _timer = xTimerCreate(
        "Timer",
        pdMS_TO_TICKS(1000),
        pdTRUE,
        NULL,
        timer_cb);
}
