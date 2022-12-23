#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gpio.h"
#include "freertos/semphr.h"
//-------------------------------------------------------------------------
// thu vien code cua ds18b20

#include <stdlib.h>
#include <string.h> //Requires by memset
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <esp_http_server.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>
#include "ds18b20.h" 


#define ESP_WIFI_SSID "Fau"
#define ESP_WIFI_PASSWORD "123456789"
#define ESP_MAXIMUM_RETRY 5

const int DS_PIN = 4;

static const char *TAG = "esp32 webserver";

/* FreeRTOS event group to signal when connected*/
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;
int wifi_connect_status = 0;

float TempC;
float TempF;

char html_page[] = "<!DOCTYPE HTML><html>\n"
                   "<head>\n"
                   "  <title>ESP-IDF DS18B20 Web Server</title>\n"
                   "  <meta http-equiv=\"refresh\" content=\"10\">\n"
                   "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                   "  <link rel=\"stylesheet\" href=\"https://use.fontawesome.com/releases/v5.7.2/css/all.css\" integrity=\"sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr\" crossorigin=\"anonymous\">\n"
                   "  <link rel=\"icon\" href=\"data:,\">\n"
                   "  <style>\n"
                   "    html {font-family: Arial; display: inline-block; text-align: center;}\n"
                   "    p {  font-size: 1.2rem;}\n"
                   "    body {  margin: 0;}\n"
                   "    .topnav { overflow: hidden; background-color: #7c0e61; color: white; font-size: 1.7rem; }\n"
                   "    .content { padding: 20px; }\n"
                   "    .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }\n"
                   "    .cards { max-width: 700px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }\n"
                   "    .reading { font-size: 2.8rem; }\n"
                   "    .card.temperatureC { color: #0e7c7b; }\n"
                   "    .card.temperatureF { color: #7c200e; }\n"
                   "  </style>\n"
                   "</head>\n"
                   "<body>\n"
                   "  <div class=\"topnav\">\n"
                   "    <h3>ESP-IDF DS18B20 WEB SERVER</h3>\n"
                   "  </div>\n"
                   "  <div class=\"content\">\n"
                   "    <div class=\"cards\">\n"
                   "      <div class=\"card temperatureC\">\n"
                   "        <h4><i class=\"fas fa-thermometer-half\"></i> TEMPERATURE</h4><p><span class=\"reading\">%.2f&deg;C</span></p>\n"
                   "      </div>\n"
                   "      <div class=\"card temperatureF\">\n"
                   "        <h4><i class=\"fas fa-thermometer-half\"></i> TEMPERATURE</h4><p><span class=\"reading\">%.2f&deg;F</span></p>\n"
                   "      </div>\n"
                   "    </div>\n"
                   "  </div>\n"
                   "</body>\n"
                   "</html>";

void DS18B20_readings()
{
	ds18b20_init(DS_PIN);
    ds18b20_requestTemperatures();

    TempC = ds18b20_get_temp();
    TempF = TempC*9/5 + 32;
		
}
 
 
 static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        wifi_connect_status = 0;
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_connect_status = 1;
    }
}

void connect_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASSWORD,
            
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");


    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);


    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASSWORD);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASSWORD);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    vEventGroupDelete(s_wifi_event_group);
}

esp_err_t send_web_page(httpd_req_t *req)
{
    int response;
    DS18B20_readings();
    char response_data[sizeof(html_page) + 50];
    memset(response_data, 0, sizeof(response_data));
    sprintf(response_data, html_page, TempC, TempF);
    response = httpd_resp_send(req, response_data, HTTPD_RESP_USE_STRLEN);

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

httpd_handle_t setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get);
    }

    return server;
}

//-----------------------------------------------------------------------------------
// LCD
#include "HD44780.h"

#define LCD_ADDR 0x27
#define SDA_PIN  21
#define SCL_PIN  22
#define LCD_COLS 16
#define LCD_ROWS 2
//-------------------------------------------------------------------------------------------

SemaphoreHandle_t xSemaphore = NULL;

#define ESP_INR_FLAG_DEFAULT 0
#define LED_PIN  27
#define PUSH_BUTTON_PIN  33

TaskHandle_t ISR = NULL;

TaskHandle_t myTaskHandle = NULL;
TaskHandle_t myTaskHandle2 = NULL;


void IRAM_ATTR button_isr_handler(void *arg){
   xSemaphoreGiveFromISR(xSemaphore, NULL);
   xTaskResumeFromISR(ISR);


  
}

void interrupt_task(void *arg){
  bool led_status = false;

  while(1){
 
    vTaskSuspend(NULL);
    led_status = !led_status;
    gpio_set_level(LED_PIN, led_status);
    vTaskDelay(pdMS_TO_TICKS(500));


  }
    }



void Demo_Task2(void *arg)
{
    char num[20];
    int i =0;
    while(1){
     if(xSemaphoreTake(xSemaphore, portMAX_DELAY))
     {
     LCD_home();
    LCD_clearScreen();
    printf("Received Message [%d] \n", xTaskGetTickCount());
    LCD_writeStr("Receive semphr");

    i=i+1;
            LCD_setCursor(0, 1);
            sprintf(num, "%d", i);
    fflush(stdin);
            LCD_writeStr(num);
    vTaskDelay(pdMS_TO_TICKS(2000));

     
 
     }
    }
}

void app_main(void)
{
//---------------------------------------
  
    // code wifi cua esp32 dung cho ds18b20
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    connect_wifi();

    if (wifi_connect_status)
    {
        setup_server();
        ESP_LOGI(TAG, "Web Server is up and running\n");
    }
    else
        ESP_LOGI(TAG, "Failed to connected with Wi-Fi, check your network Credentials\n");


//-------------------------------------------------------------------------
// code push button va led, LCD
  gpio_pad_select_gpio(PUSH_BUTTON_PIN);
  gpio_pad_select_gpio(LED_PIN);

  gpio_set_direction(PUSH_BUTTON_PIN, GPIO_MODE_INPUT);
  gpio_set_direction(LED_PIN ,GPIO_MODE_OUTPUT);

  gpio_set_intr_type(PUSH_BUTTON_PIN, GPIO_INTR_POSEDGE);

  gpio_install_isr_service(ESP_INR_FLAG_DEFAULT);

  gpio_isr_handler_add(PUSH_BUTTON_PIN, button_isr_handler, NULL);

LCD_init(LCD_ADDR, SDA_PIN, SCL_PIN, LCD_COLS, LCD_ROWS);
   xSemaphore = xSemaphoreCreateBinary();
   xTaskCreate(Demo_Task2, "Demo_Task2", 4096, NULL,10, &myTaskHandle2);

   xTaskCreate(interrupt_task, "interrupt_task", 4096, NULL, 10, &ISR);
}