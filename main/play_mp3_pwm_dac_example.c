/* Play mp3 file by audio pipeline

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "pwm_stream.h"
#include "mp3_decoder.h"
#include "board.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"

#include "esp_clk_tree.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

#include "task_monitor.h"

#define CONFIG_PLAY_OUTPUT_PWM
static const char *TAG = "MP3_PWM_DAC_EXAMPLE";
/*
   To embed it in the app binary, the mp3 file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
//extern const uint8_t adf_music_mp3_start[] asm("_binary_adf_music_mp3_start");
//extern const uint8_t adf_music_mp3_end[]   asm("_binary_adf_music_mp3_end");
// int mp3_music_read_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
// {
//     static int mp3_pos;
//     int read_size = adf_music_mp3_end - adf_music_mp3_start - mp3_pos;
//     if (read_size == 0) {
//         return AEL_IO_DONE;
//     } else if (len < read_size) {
//         read_size = len;
//     }
//     memcpy(buf, adf_music_mp3_start + mp3_pos, read_size);
//     mp3_pos += read_size;
//     return read_size;
// }
static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void check_ESP_ERROR(esp_err_t result, char * str){
    if (result == ESP_OK){
    ESP_LOGI(TAG, "%s returned OK", str);
    } else {
    ESP_LOGI(TAG, "%s returned FAIL",str);
    }
}

void app_main(void)
{
    // delay needed to let cdc serial port sort itself out before running everything else
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    initialize_nvs();
    ESP_LOGI(TAG, "Starting");

audio_pipeline_handle_t pipeline;
audio_element_handle_t http_stream_reader, mp3_decoder, output_stream_writer;


#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    task_monitor();

//    while(1) vTaskDelay(1000 / portTICK_PERIOD_MS);

//    return;

    uint32_t fValue;
    esp_clk_tree_src_get_freq_hz( SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,&fValue);
    ESP_LOGI(TAG,"CPU clock frequency %ld",fValue);
        
    ESP_LOGI(TAG, "[ 2 ] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[2.1] Create http stream to read data");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
//    http_cfg.out_rb_size = 40 * 1024;
    http_stream_reader = http_stream_init(&http_cfg);


    ESP_LOGI(TAG, "[2.1] Create output stream to write data to codec chip");
    pwm_stream_cfg_t pwm_cfg = PWM_STREAM_CFG_DEFAULT();
//    pwm_cfg.pwm_config.gpio_num_left = CONFIG_PWM_LEFT_OUTPUT_GPIO_NUM;
//    pwm_cfg.pwm_config.gpio_num_right = CONFIG_PWM_RIGHT_OUTPUT_GPIO_NUM;
    pwm_cfg.pwm_config.gpio_num_left = CONFIG_PWM_RIGHT_OUTPUT_GPIO_NUM;
    pwm_cfg.pwm_config.gpio_num_right = CONFIG_PWM_LEFT_OUTPUT_GPIO_NUM;

//    Override the menuconfig [Example Configuration  --->] values
//    pwm_cfg.pwm_config.ledc_channel_left =LEDC_CHANNEL_2;
//    pwm_cfg.pwm_config.ledc_channel_right =LEDC_CHANNEL_3;

//    pwm_cfg.pwm_config.gpio_num_left = CONFIG_PWM_LEFT_OUTPUT_GPIO_NUM;
//    pwm_cfg.pwm_config.gpio_num_right = CONFIG_PWM_RIGHT_OUTPUT_GPIO_NUM;
    output_stream_writer = pwm_stream_init(&pwm_cfg);

//
//
//    NOTE: there area a couple of problems in ESP-ADF for the pwm_stream audio stream
//    It will not work with aa ESP32-S3 without changing pwm_stream.c because code that is processor
//      dependant does not cover the ESP32S3
//
//      this is covered in https://github.com/espressif/esp-adf/issues/1499
//
//    The second is a little more subtle and seems to be associated with the optimiser for the ESP compiler
//      It would appear to ignore the inline function qualifier/think it knows better under some circumstances
//      There are 2 functions which are called from the isr routine which is in IRAM which are defined as inline
//      This should result in them being inline and consequently in IRAM but they are not inline and are in normal codespace
//      This is a problem if the routinees are not in the cache and PSRAM is being used.
//      The fix is to add the IRAM_ATTR to the functions ledc_set_left_duty_fast() and ledc_set_right_duty_fast()
//
//      snippit out of pwm_stream.c
//
//
// compiler may not inline this do not know under which circumstances so make sure it uses IRAM for when PSRAM is being used and isr is in IRAM but this is not and not in cache when needed
// //static inline void ledc_set_left_duty_fast(uint32_t duty_val)
// static inline IRAM_ATTR void ledc_set_left_duty_fast(uint32_t duty_val)
// {
//     *g_ledc_left_duty_val = (duty_val) << 4;
//     *g_ledc_left_conf0_val |= 0x00000014;
//     *g_ledc_left_conf1_val |= 0x80000000;
// }

// // compiler may not inline this do not know under which circumstances so make sure it uses IRAM for when PSRAM is being used  and isr is in IRAM but this is not and not in cache when needed
// //static inline void ledc_set_right_duty_fast(uint32_t duty_val)
// static inline IRAM_ATTR void ledc_set_right_duty_fast(uint32_t duty_val)
// {
//     *g_ledc_right_duty_val = (duty_val) << 4;
//     *g_ledc_right_conf0_val |= 0x00000014;
//     *g_ledc_right_conf1_val |= 0x80000000;
// }
//
// It equally has prblems on the esp32C6
//

    ESP_LOGI(TAG, "[2.2] Create mp3 decoder to decode mp3 file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(TAG, "[2.3] Register all elements to audio pipeline");
    check_ESP_ERROR(audio_pipeline_register(pipeline, http_stream_reader,   "http"),"Register to pipeline http_stream");
    check_ESP_ERROR(audio_pipeline_register(pipeline, mp3_decoder,          "mp3"),"Register to pipeline mp3_decoder");
    check_ESP_ERROR(audio_pipeline_register(pipeline, output_stream_writer, "output"),"Register to pipeline output_stream_writer");

    ESP_LOGI(TAG, "[2.4] Link it together [mp3_music_read_cb]-->mp3_decoder-->output_stream-->[pa_chip]");
    const char *link_tag[3] = {"http","mp3", "output"};
    check_ESP_ERROR(audio_pipeline_link(pipeline, &link_tag[0], 3), "Link elements");

    ESP_LOGI(TAG, "[2.6] Set up  uri (http as http_stream, mp3 as mp3 decoder, and default output is i2s)");
    // try either of these URLs which were working or find a known working one
    // the https one requires the SSL to be setup properly

//    check_ESP_ERROR(audio_element_set_uri(http_stream_reader, "http://0n-80s.radionetz.de:8000/0n-70s.mp3"), "Set URL");
    check_ESP_ERROR(audio_element_set_uri(http_stream_reader, "https://icecast.thisisdax.com/GoldMP3"), "Set URL");

    ESP_LOGI(TAG, "[ 3 ] Start and wait for Wi-Fi network");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    //set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    check_ESP_ERROR(esp_periph_start(set, wifi_handle),"Wifi Start");
    check_ESP_ERROR(periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY),"wifi connect");

    ESP_LOGI(TAG, "[ 3 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    //evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[3.1] Listening event from all elements of pipeline");
    check_ESP_ERROR(audio_pipeline_set_listener(pipeline, evt),"Set Listener");

    ESP_LOGI(TAG, "[3.2] Listening event from peripherals");
    check_ESP_ERROR(audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt), "Set Listener for events");

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
//    check_ESP_ERROR(audio_element_run(http_stream_reader),"http_stream_reader run");
//    check_ESP_ERROR(audio_element_run(mp3_decoder),"mp3_decoder run");
//    check_ESP_ERROR(audio_element_run(output_stream_writer),"output_stream_writer run");

    check_ESP_ERROR(audio_pipeline_run(pipeline), "Start Audio Pipeline");

    ESP_LOGI(TAG, "[ 5 ] Listen for all pipeline events");

//    task_monitor();
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
//        esp_err_t ret = audio_event_iface_listen(evt, &msg, 100000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
            && msg.source == (void *) mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            audio_element_set_music_info(output_stream_writer, music_info.sample_rates, music_info.channels, music_info.bits);
#ifdef CONFIG_PLAY_OUTPUT_PWM
            pwm_stream_set_clk(output_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
#elif (defined(CONFIG_PLAY_OUTPUT_DAC))
            i2s_stream_set_clk(output_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
#endif
//            pwm_stream_set_clk(output_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }

/*
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);
            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);
            audio_element_set_music_info(output_stream_writer, music_info.sample_rates, music_info.channels, music_info.bits);
#ifdef CONFIG_PLAY_OUTPUT_PWM
            pwm_stream_set_clk(output_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
#elif (defined(CONFIG_PLAY_OUTPUT_DAC))
            i2s_stream_set_clk(output_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
#endif
            continue;
        }
*/
        /* Stop when the last pipeline element (output_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) output_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGI(TAG, "[ * ] Stop event received");
            break;
        }
        if (msg.source == (void *) output_stream_writer){
            ESP_LOGI(TAG,"Message from http_stream_reader cmd: %d data: %d",msg.cmd, (int)msg.data);
        }
        if (msg.source == (void *) mp3_decoder){
            ESP_LOGI(TAG,"Message from mp3_decoder cmd: %d data: %d",msg.cmd, (int)msg.data);
        }
        if (msg.source == (void *) output_stream_writer){
            ESP_LOGI(TAG,"Message from output_stream_writer cmd: %d data: %d",msg.cmd, (int)msg.data);
        }
    }

    ESP_LOGI(TAG, "[ 6 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);
    audio_pipeline_unregister(pipeline, mp3_decoder);
    audio_pipeline_unregister(pipeline, output_stream_writer);

    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(mp3_decoder);
    audio_element_deinit(output_stream_writer);
    esp_periph_set_destroy(set);

}