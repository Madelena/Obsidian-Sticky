// =============================================================================
// PDM MIC
// =============================================================================
// I2S PDM RX on GPIO19/20 with mic power on GPIO38.
#include "audio/pdm_mic.h"

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "pin_config.h"

namespace pdm_mic {
namespace {

constexpr const char *kTag = "pdm_mic";
constexpr uint32_t kPowerSettleMs = 20;
// The mic outputs a DC settle ramp for the first tens of milliseconds.
constexpr size_t kDiscardSamples = kSampleRateHz / 10;

i2s_chan_handle_t s_rx = nullptr;
bool s_running = false;

// Sets the mic power rail.
esp_err_t set_power(bool on)
{
    return gpio_set_level(static_cast<gpio_num_t>(PIN_MIC_EN), on ? 1 : 0);
}

}  // namespace


esp_err_t init()
{
    if (s_rx != nullptr) {
        return ESP_OK;
    }

    // GPIO19/20 are the native USB pads. The USB-Serial-JTAG function is a
    // dedicated pad connection that survives deep sleep, so the PHY pad must
    // be disabled before the GPIO matrix can route them to I2S. Flashing and
    // logs use the external UART bridge, so nothing is lost.
    usb_serial_jtag_ll_phy_enable_pad(false);
    gpio_reset_pin(static_cast<gpio_num_t>(PIN_MIC_CLK));
    gpio_reset_pin(static_cast<gpio_num_t>(PIN_MIC_DATA));

    gpio_config_t power = {};
    power.pin_bit_mask = 1ULL << PIN_MIC_EN;
    power.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&power), kTag, "mic power pin");
    ESP_RETURN_ON_ERROR(set_power(false), kTag, "mic power off");

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel.dma_desc_num = 6;
    channel.dma_frame_num = 512;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, nullptr, &s_rx), kTag, "i2s channel");

    i2s_pdm_rx_config_t pdm = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kSampleRateHz),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = static_cast<gpio_num_t>(PIN_MIC_CLK),
            .din = static_cast<gpio_num_t>(PIN_MIC_DATA),
            .invert_flags = {.clk_inv = false},
        },
    };
    esp_err_t err = i2s_channel_init_pdm_rx_mode(s_rx, &pdm);
    if (err != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        return err;
    }
    ESP_LOGI(kTag, "PDM RX ready: %lu Hz mono, clk=%d data=%d power=%d",
             static_cast<unsigned long>(kSampleRateHz), PIN_MIC_CLK, PIN_MIC_DATA, PIN_MIC_EN);
    return ESP_OK;
}


esp_err_t start()
{
    if (s_rx == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_running) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(set_power(true), kTag, "mic power on");
    vTaskDelay(pdMS_TO_TICKS(kPowerSettleMs));
    esp_err_t err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        set_power(false);
        return err;
    }
    s_running = true;

    int16_t scratch[256];
    size_t discarded = 0;
    while (discarded < kDiscardSamples) {
        size_t got = 0;
        if (read(scratch, 256, got, 200) != ESP_OK || got == 0) {
            break;
        }
        discarded += got;
    }
    return ESP_OK;
}


esp_err_t read(int16_t *dest, size_t max_samples, size_t &got, uint32_t timeout_ms)
{
    got = 0;
    if (!s_running || dest == nullptr || max_samples == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bytes = 0;
    ESP_RETURN_ON_ERROR(i2s_channel_read(s_rx, dest, max_samples * sizeof(int16_t), &bytes, timeout_ms),
                        kTag, "i2s read");
    got = bytes / sizeof(int16_t);
    return ESP_OK;
}


esp_err_t stop()
{
    if (!s_running) {
        return set_power(false);
    }
    s_running = false;
    const esp_err_t err = i2s_channel_disable(s_rx);
    const esp_err_t power_err = set_power(false);
    return err != ESP_OK ? err : power_err;
}

}  // namespace pdm_mic
