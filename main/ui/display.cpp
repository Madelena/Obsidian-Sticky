// =============================================================================
// DISPLAY
// =============================================================================
// SSD1677 bring-up over SPI2 plus the 180-degree rotation between the logical
// canvas and the panel's native buffer. Sequence follows Seeed's dashboard
// demo; pin numbers come from pin_config.h.
#include "ui/display.h"

#include <mutex>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epaper_panel.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "ui/canvas.h"

namespace display {
namespace {

constexpr const char *kTag = "display";
constexpr int kPartialsBeforeFull = 20;

seeed_epaper_panel_handle_t s_panel = nullptr;
spi_device_handle_t s_spi = nullptr;
uint8_t *s_rotated = nullptr;
int s_partials_since_full = 0;
bool s_promotion = true;

// The waveform blocks for about a second on a partial and two on a full, and
// the panel shares nothing with the radio, so the wait belongs on its own
// task. Callers rotate the canvas into s_rotated themselves, which is a few
// milliseconds, then hand the mode over and carry on with the network.
QueueHandle_t s_render_queue = nullptr;
SemaphoreHandle_t s_render_done = nullptr;
std::mutex s_refresh_mutex;
bool s_render_busy = false;

// Reverses the bit order of one byte, which is half of a 180-degree rotation.
inline uint8_t reverse_bits(uint8_t value)
{
    value = static_cast<uint8_t>(((value & 0xF0U) >> 4) | ((value & 0x0FU) << 4));
    value = static_cast<uint8_t>(((value & 0xCCU) >> 2) | ((value & 0x33U) << 2));
    value = static_cast<uint8_t>(((value & 0xAAU) >> 1) | ((value & 0x55U) << 1));
    return value;
}

// Rotates the packed canvas 180 degrees: reverse the bytes, then each byte.
void rotate_canvas()
{
    const uint8_t *source = canvas::data();
    for (int i = 0; i < canvas::kSize; ++i) {
        s_rotated[i] = reverse_bits(source[canvas::kSize - 1 - i]);
    }
}

// Rotates and sends the whole frame with the requested waveform.
esp_err_t push(seeed_epaper_refresh_mode_t mode)
{
    if (s_panel == nullptr || s_rotated == nullptr || canvas::data() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t started = esp_timer_get_time();
    const seeed_epaper_area_t full = {0, 0, canvas::kWidth, canvas::kHeight};
    const esp_err_t err = seeed_epaper_panel_refresh_area(
        s_panel, &full, s_rotated, canvas::kStride, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, mode);
    // This blocks the calling task for the whole waveform, so it is on the
    // critical path of a note. Worth knowing against the network timings.
    ESP_LOGI(kTag, "%s refresh took %u ms",
             mode == SEEED_EPAPER_REFRESH_FULL ? "full" : "partial",
             static_cast<unsigned>((esp_timer_get_time() - started) / 1000));
    return err;
}


// Waits out one panel waveform, so the task that asked for it does not.
void render_task(void *)
{
    while (true) {
        seeed_epaper_refresh_mode_t mode = SEEED_EPAPER_REFRESH_FULL;
        if (xQueueReceive(s_render_queue, &mode, portMAX_DELAY) == pdTRUE) {
            push(mode);
            xSemaphoreGive(s_render_done);
        }
    }
}

// Blocks until an earlier refresh has finished with s_rotated. The caller
// must hold s_refresh_mutex.
void wait_idle_locked()
{
    if (!s_render_busy) {
        return;
    }
    xSemaphoreTake(s_render_done, portMAX_DELAY);
    s_render_busy = false;
}

// Rotates the canvas and hands the waveform to the render task.
esp_err_t start_refresh(seeed_epaper_refresh_mode_t mode)
{
    if (s_panel == nullptr || s_rotated == nullptr || canvas::data() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    std::lock_guard<std::mutex> lock(s_refresh_mutex);
    wait_idle_locked();
    rotate_canvas();
    s_render_busy = true;
    xQueueSend(s_render_queue, &mode, portMAX_DELAY);
    return ESP_OK;
}

}  // namespace


esp_err_t init()
{
    if (s_panel != nullptr) {
        return ESP_OK;
    }

    gpio_config_t power = {};
    power.pin_bit_mask = 1ULL << PIN_EPD_EN;
    power.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&power), kTag, "panel power pin");
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(PIN_EPD_EN), 1), kTag, "panel power on");
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t bus = {};
    bus.mosi_io_num = PIN_EPD_MOSI;
    bus.miso_io_num = PIN_EPD_MISO;
    bus.sclk_io_num = PIN_EPD_CLK;
    // Unused data pins must be -1; a zero here would claim GPIO0, the sensor SCL.
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = canvas::kSize;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), kTag, "spi2");

    spi_device_interface_config_t device = {};
    device.clock_speed_hz = 10 * 1000 * 1000;
    device.mode = 0;
    device.spics_io_num = PIN_EPD_CS;
    device.queue_size = 1;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &device, &s_spi), kTag, "spi device");

    seeed_epaper_panel_config_t config = {};
    config.spi_handle = s_spi;
    config.pin_dc = static_cast<gpio_num_t>(PIN_EPD_DC);
    config.pin_rst = static_cast<gpio_num_t>(PIN_EPD_RST);
    config.pin_busy = static_cast<gpio_num_t>(PIN_EPD_BUSY);
    // Panel power is driven here, not by the driver, so sleep() can cut it.
    config.pin_enable = GPIO_NUM_NC;
    config.busy_timeout_ms = 10000;
    config.reset_low_ms = 10;
    config.reset_high_ms = 10;
    config.busy_level = 1;
    config.enable_level = 1;
    config.mirror_x = true;
    ESP_RETURN_ON_ERROR(seeed_epaper_new_panel(SEEED_EPAPER_PANEL_SSD1677, &config, &s_panel),
                        kTag, "panel");

    s_rotated = static_cast<uint8_t *>(
        heap_caps_malloc(canvas::kSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_rotated == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    s_render_queue = xQueueCreate(1, sizeof(seeed_epaper_refresh_mode_t));
    s_render_done = xSemaphoreCreateBinary();
    if (s_render_queue == nullptr || s_render_done == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(render_task, "render", 4096, nullptr, 4, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "Panel ready, 800x480 mono, rotated 180");
    return ESP_OK;
}


esp_err_t refresh_full()
{
    s_partials_since_full = 0;
    return start_refresh(SEEED_EPAPER_REFRESH_FULL);
}


esp_err_t refresh_partial()
{
    // A suppressed partial does not count either, or a recording's six
    // hundred of them would land the promotion on the very next screen, which
    // is "Transcribing" and sits in the path the user is waiting on.
    if (s_promotion && ++s_partials_since_full >= kPartialsBeforeFull) {
        return refresh_full();
    }
    return start_refresh(SEEED_EPAPER_REFRESH_PARTIAL);
}


void set_promotion(bool enabled)
{
    s_promotion = enabled;
}


void wait_idle()
{
    std::lock_guard<std::mutex> lock(s_refresh_mutex);
    wait_idle_locked();
}


esp_err_t sleep()
{
    if (s_panel == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    // The image left on the panel is the one the user keeps looking at, so the
    // controller must not be put to sleep with a waveform still running.
    std::lock_guard<std::mutex> lock(s_refresh_mutex);
    wait_idle_locked();
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_sleep(s_panel), kTag, "panel sleep");
    return gpio_set_level(static_cast<gpio_num_t>(PIN_EPD_EN), 0);
}

}  // namespace display
