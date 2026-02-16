#include "audio_manager.h"
#include "config.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

#define I2S_PORT       I2S_NUM_0
#define SAMPLE_RATE    44100
#define DMA_BUF_COUNT  8
#define DMA_BUF_LEN    256

static bool audioReady   = false;
static bool audioEnabled = true;

void audioSetEnabled(bool on) { audioEnabled = on; }
bool audioIsEnabled() { return audioEnabled; }

void audioSetup() {
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = SAMPLE_RATE;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = DMA_BUF_COUNT;
    i2s_config.dma_buf_len = DMA_BUF_LEN;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = true;

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = I2S_BCLK;
    pin_config.ws_io_num = I2S_LRCK;
    pin_config.data_out_num = I2S_DOUT;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Audio] I2S driver install failed: %d\n", err);
        return;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[Audio] I2S set pin failed: %d\n", err);
        return;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    audioReady = true;
    Serial.println("[Audio] I2S MAX98357A initialized");
}

void playTone(uint16_t freqHz, uint16_t durationMs) {
    if (!audioReady || !audioEnabled) return;

    uint32_t totalSamples = (uint32_t)SAMPLE_RATE * durationMs / 1000;
    const int bufSize = 256;
    int16_t buf[bufSize];
    float phase = 0.0f;
    float phaseInc = 2.0f * M_PI * freqHz / SAMPLE_RATE;
    int16_t amplitude = 12000; // ~37% volume to avoid clipping

    uint32_t written = 0;
    while (written < totalSamples) {
        int chunk = min((uint32_t)bufSize, totalSamples - written);
        for (int i = 0; i < chunk; i++) {
            buf[i] = (int16_t)(amplitude * sinf(phase));
            phase += phaseInc;
            if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
        }
        size_t bytesWritten = 0;
        i2s_write(I2S_PORT, buf, chunk * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        written += chunk;
    }

    // Brief silence to flush DMA
    memset(buf, 0, sizeof(buf));
    size_t dummy;
    i2s_write(I2S_PORT, buf, 64 * sizeof(int16_t), &dummy, portMAX_DELAY);
}

void playAlertUp() {
    playTone(800, 100);
    playTone(1200, 100);
}

void playAlertDown() {
    playTone(1200, 100);
    playTone(800, 100);
}

void playStartup() {
    // C5-E5-G5 chime
    playTone(523, 80);  // C5
    playTone(659, 80);  // E5
    playTone(784, 80);  // G5
}

void playTap() {
    playTone(2000, 30);
}
