/**
 * @file audio.h
 * @brief 音频模块驱动 — LMD2718 (PDM 硅麦) + NS4168 (I2S 功放)
 *
 * 硬件接线（ESP32-S3）：
 *   LMD2718 CLK  → GPIO4  (I2S0 BCLK)
 *   LMD2718 DATA → GPIO5  (I2S0 DIN)
 *   NS4168 LRCLK → GPIO6  (I2S1 WS)
 *   NS4168 SDATA → GPIO7  (I2S1 DOUT)
 *   NS4168 BCLK  → GPIO15 (I2S1 BCLK)
 *   CTRL          → GND    (左声道，不关断)
 *
 * 设计说明：
 *   I2S0 = PDM RX → LMD2718 麦克风采集（硬件 PDM→PCM 转换）
 *   I2S1 = STD TX → NS4168 音频播放（标准 I2S Philips 格式）
 *   两个控制器独立运行，互不干扰。
 */

#ifndef __AUDIO_H__
#define __AUDIO_H__

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2S 采样率（两路统一） */
#define AUDIO_SAMPLE_RATE       16000       /* 16 kHz */
#define AUDIO_BITS_PER_SAMPLE   16          /* 16-bit PCM */

/* 麦克风帧回调：每采集到一帧 PCM 数据时调用 */
typedef void (*audio_record_cb_t)(const int16_t *pcm, size_t samples, void *user_data);

/**
 * @brief 初始化音频模块
 *
 * 依次初始化 I2S1 (NS4168 TX) 和 I2S0 (LMD2718 RX)。
 * 初始化成功后播放一段 1kHz/0.5s 自检音。
 *
 * @return ESP_OK 成功，否则失败
 */
esp_err_t audio_init(void);

/* ================================================================
 * 播放接口（阶段 1）
 * ================================================================ */

/**
 * @brief 播放 PCM 数据（阻塞，等待播放完成）
 *
 * @param data  PCM 数据（16-bit signed, mono）
 * @param samples  样本数
 * @return ESP_OK 成功
 */
esp_err_t audio_play(const int16_t *data, size_t samples);

/* ================================================================
 * 录音接口（阶段 2）
 * ================================================================ */


/**
 * @brief 开始录音（创建内部采集任务，通过回调获取 PCM 数据）
 *
 * 启用 I2S0 PDM RX，内部创建任务按帧读取 PCM。
 * 每帧（640 样本 ≈ 40ms @ 16kHz）调用回调一次。
 *
 * @param cb    回调函数（传入 PCM 数据、样本数、用户上下文）
 * @param ctx   用户上下文
 * @return ESP_OK 成功
 */
esp_err_t audio_record_start(audio_record_cb_t cb, void *ctx);

/**
 * @brief 停止录音（停止采集，删除内部任务）
 *
 * @return ESP_OK 成功
 */
esp_err_t audio_record_stop(void);

/**
 * @brief 获取当前 RMS 能量值（串口调试用）
 *
 * 每次录音帧到达时内部计算，非实时调用。
 *
 * @return RMS 值（0~32767）
 */
int audio_get_rms(void);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_H__ */
