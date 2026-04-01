/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"

/* config Macro for your end */
//#define ESP_TIMER_RESOURCE
//#define GPTIMER_RESOURCE
#define LEDC_RESOURCE

#if defined(ESP_TIMER_RESOURCE)
//for esp_timer
#include "esp_timer.h"
#include "driver/gpio.h"

#elif defined(GPTIMER_RESOURCE)
//for GPTIMER
#include "driver/gptimer.h"
#include "driver/gpio.h"

#else
//for LEDC
#include "driver/ledc.h"
#endif

#if defined(GPTIMER_RESOURCE)
gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};

gptimer_alarm_config_t alarm_config = {
    //.reload_count = 0,
    .alarm_count = 2000000, // Set the alarm to trigger when the count reaches 2,000,000 (i.e., 2 seconds)
    .flags.auto_reload_on_alarm = false, // Enable auto-reload function, so the alarm value will be reloaded to timer's counter value when the alarm event occurs
};

typedef struct {
    uint64_t event_count;
    uint64_t event_alarm_value;
} example_queue_element_t;

example_queue_element_t ele = {
    .event_count = 0,
    .event_alarm_value = 0,
};

QueueHandle_t gptimer_queue = NULL;

static bool IRAM_ATTR timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    static int alarm_cnt = 0;
    BaseType_t high_task_awoken = pdFALSE;

    gptimer_queue = (QueueHandle_t)user_data;
    ele.event_count = edata->count_value;
    ele.event_alarm_value = edata->alarm_value;
    xQueueSendFromISR(gptimer_queue, &ele, &high_task_awoken);

    alarm_cnt++;
    
    if(alarm_cnt % 2 == 0) {
        alarm_config.alarm_count = edata->alarm_value + 900000; // Next alarm in 1s from the current alarm
        gpio_set_level(1, 0);
    } else {
        alarm_config.alarm_count = edata->alarm_value + 100000; // Next alarm in 1s from the current alarm
        gpio_set_level(1, 1);
    }
    // Update the alarm value
    gptimer_set_alarm_action(timer, &alarm_config);

    // return whether we need to yield at the end of ISR
    return high_task_awoken == pdTRUE;
}

static void gptimer_event_task(void *arg)
{
    while(1) {
        if(gptimer_queue != NULL && xQueueReceive(gptimer_queue, &ele, portMAX_DELAY) == pdTRUE) {
            printf("Queue event, count value: %" PRIu64 ", alarm value: %" PRIu64 "\n", ele.event_count, ele.event_alarm_value);
        }
    }
}

static void gptimer_config(void)
{

    gpio_reset_pin(1);
    gpio_set_direction(1, GPIO_MODE_OUTPUT);
    gpio_set_level(1, 0);

    // Create a timer instance
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    
    // Set the timer's alarm action
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_on_alarm_cb, // Call the user callback function when the alarm event occurs
    };

    // Register timer event callback functions, allowing user context to be carried
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, gptimer_queue));
    // Enable the timer
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    // Start the timer
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    // Create a task to handle timer events from the timer's ISR
    xTaskCreate(gptimer_event_task, "gptimer_event_task", 2048, NULL, 5, NULL);

#if 0
    ESP_ERROR_CHECK(gptimer_stop(gptimer));
    ESP_ERROR_CHECK(gptimer_disable(gptimer));
    ESP_ERROR_CHECK(gptimer_del_timer(gptimer));
#endif
}

#endif //GPTIMER_RESOURCE

#if defined(LEDC_RESOURCE)
/* by JN the following configuration is the lowest frequency & duty combination we can get...otherwise error*/
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (1) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_14_BIT // Set duty resolution to 14 bits
#define LEDC_DUTY               (1638) // Set duty to 10%. ((2 ** 14) - 1) * 10% = 1638
#define LEDC_FREQUENCY          (2) // Frequency in Hertz. Set frequency at 2 Hz

static void ledc_config(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_USE_RC_FAST_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    // Set duty to 20%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

}
#endif

#if defined(ESP_TIMER_RESOURCE)

static void periodic_timer_callback(void* arg)
{
    static int timer_cnt = 0;
    timer_cnt++;
    printf("Timer callback called, count: %d\n", timer_cnt);
    if(timer_cnt % 2 == 0) {
        gpio_set_level(1, 0);
    } else {
        gpio_set_level(1, 1);
    }
}

static void esp_timer_config(void)
{
    gpio_reset_pin(1);
    gpio_set_direction(1, GPIO_MODE_OUTPUT);
    gpio_set_level(1, 0);

    esp_timer_handle_t periodic_timer;
    esp_timer_create_args_t periodic_timer_args = {
        .callback = periodic_timer_callback,
        .name = "periodic"
    };
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000)); // 1s
}

#endif

static int alert_cnt = 0;
void vAlertTask(void *pvParameters) {
    while(1) {
        alert_cnt++;
        printf("#%d, Hello world!\r\n", alert_cnt);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void get_chip_info(void)
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
}

void app_main(void)
{
#if defined(GPTIMER_RESOURCE)

    // this should be called before any other gptimer APIs are called,
    // but only needs to be called once in the whole program, so we put it here
    gptimer_queue = xQueueCreate(10, sizeof(example_queue_element_t));
    if (!gptimer_queue) {
        printf("Creating queue failed\n");
        return;
    }

    gptimer_config();

#elif defined(LEDC_RESOURCE)

    ledc_config();

#else //ESP_TIMER_RESOURCE

    esp_timer_config();

#endif
    //common functions
    get_chip_info();
    xTaskCreate(vAlertTask, "Alert_Task", 2048, NULL, 5, NULL);
}
