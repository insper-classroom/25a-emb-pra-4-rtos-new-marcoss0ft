#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>


const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

const uint TRIGGER_PIN = 16;
const uint ECHO_PIN = 17;


QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

void glx_draw_fill_rect(ssd1306_t *disp, int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        gfx_draw_line(disp, x, y + j, x + w - 1, y + j);
    }
}

void draw_distance_bar(ssd1306_t *disp, double distance) {
    const double maxDistance = 90;
    int bar_length = (int)((distance / maxDistance) * 128.0);
    
    if (bar_length > 128) {
        bar_length = 128;
    } else if (bar_length < 0) {
        bar_length = 0;
    }

    glx_draw_fill_rect(disp, 0, 16, bar_length, 8);
}

void oled1_btn_led_init(void) {
    gpio_init(LED_1_OLED);
    gpio_set_dir(LED_1_OLED, GPIO_OUT);

    gpio_init(LED_2_OLED);
    gpio_set_dir(LED_2_OLED, GPIO_OUT);

    gpio_init(LED_3_OLED);
    gpio_set_dir(LED_3_OLED, GPIO_OUT);

    gpio_init(BTN_1_OLED);
    gpio_set_dir(BTN_1_OLED, GPIO_IN);
    gpio_pull_up(BTN_1_OLED);

    gpio_init(BTN_2_OLED);
    gpio_set_dir(BTN_2_OLED, GPIO_IN);
    gpio_pull_up(BTN_2_OLED);

    gpio_init(BTN_3_OLED);
    gpio_set_dir(BTN_3_OLED, GPIO_IN);
    gpio_pull_up(BTN_3_OLED);
}


void sensor_init(void) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_pull_down(ECHO_PIN);
}


void pin_callback(uint gpio, uint32_t events) {
    static uint64_t t_rise = 0;

    if (events & GPIO_IRQ_EDGE_RISE) {
        t_rise = to_us_since_boot(get_absolute_time());
    } 
    else if (events & GPIO_IRQ_EDGE_FALL) {
        uint64_t dt = to_us_since_boot(get_absolute_time()) - t_rise;

        xQueueSendFromISR(xQueueTime, &dt, NULL);
    }
}


void trigger_task(void *p) {
    sensor_init();

    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10/1000));
        gpio_put(TRIGGER_PIN, 0);
        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void echo_task(void *p) {
    double distance;
    uint64_t dt;

    while (1) {
        if (xQueueReceive(xQueueTime, &dt, pdMS_TO_TICKS(100)) == pdTRUE) {
            distance = (dt / 1000000.0) * 34000.0 / 2.0;
            xQueueSend(xQueueDistance, &distance, pdMS_TO_TICKS(100));
        }
    }
}

void oled_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando btn and LEDs\n");
    oled1_btn_led_init();

    char distanceStr[16];
    double distance;

    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(100)) == pdTRUE) {
            gfx_clear_buffer(&disp);
            if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(100)) == pdTRUE) {
                snprintf(distanceStr, sizeof(distanceStr), "Dist: %.2f cm", distance);
                gfx_draw_string(&disp, 0, 0, 1, distanceStr);
                draw_distance_bar(&disp, distance);
            } else {
                gfx_draw_string(&disp, 0, 0, 1, "Falha no sensor");
            }
            gfx_show(&disp);
        }
    }
}

int main() {
    stdio_init_all();

    gpio_set_irq_enabled_with_callback(
        ECHO_PIN, 
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, 
        true, 
        &pin_callback
    );
    xQueueTime = xQueueCreate(32, sizeof(uint64_t));
    xQueueDistance = xQueueCreate(32, sizeof(double));
    xSemaphoreTrigger = xSemaphoreCreateBinary();
    xTaskCreate(oled_task,    "OLED_TASK",    4096, NULL, 1, NULL);
    xTaskCreate(trigger_task, "TRIGGER_TASK", 4096, NULL, 1, NULL);
    xTaskCreate(echo_task,    "ECHO_TASK",    4096, NULL, 1, NULL);
    vTaskStartScheduler();


    while (true)
        ;
}
