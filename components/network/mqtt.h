/**
 * @file mqtt.h
 * @brief MQTT 客户端模块
 *
 * 在 WiFi 获取到 IP 后启动 MQTT 连接，时间同步后每秒刷新主页状态栏时间。
 * SNTP 校时由 sntp.h 独立管理，在 wifi_init_sta() 之后由 main.c 启动。
 */

#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 MQTT 客户端（内部创建 mqtt_time_task）
 *
 * 内部流程：
 *   1. 等待 WiFi 连接（WIFI_CONNECTED_BIT）
 *   2. 启动 MQTT 客户端（连接 broker.emqx.io:1883）
 *   3. 时间同步后每秒更新主页时间显示
 *
 * @return void
 */
void mqtt_app_start(void);

/**
 * @brief 检查 MQTT 客户端是否已连接
 * @return true 已连接, false 未连接
 */
bool mqtt_is_connected(void);

/**
 * @brief 发布音频 PCM 数据（Base64 编码后发送）
 *
 * @param pcm    PCM 数据（16-bit signed mono）
 * @param samples 样本数
 *
 * @return void
 */
void mqtt_publish_audio(const int16_t *pcm, size_t samples);

/**
 * @brief 启动音频采集 → MQTT 发布任务
 *
 * 等待 MQTT 连接就绪后，启动 PDM 麦克风录音，
 * 每 12 帧（~480ms）累计发布一次 Base64 PCM 数据。
 *
 * @return void
 */
void mqtt_audio_test_start(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_H */
