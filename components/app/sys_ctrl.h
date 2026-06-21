#ifndef __SYS_CTRL_H
#define __SYS_CTRL_H

/**
 * @brief 启动系统控制任务
 *
 * 创建 10ms 周期的 sys_ctrl_task，替代原来的 key_task。
 * 每个周期：KeyInd_Scan() → 读事件组 → 按显示状态分发。
 *
 * @return void
 */
void sys_ctrl_start(void);

#endif
