#include <NightMare.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" void app_main()
{
    introNightMareESP();
    startNightMareESP();
    for (;;)
    {
        tickNightMareESP();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
