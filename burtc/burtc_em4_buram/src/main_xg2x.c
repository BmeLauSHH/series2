/***************************************************************************//**
 * @file main_xg2x.c
 * @brief 该项目使用 BURTC（备份实时计数器）来唤醒设备从 EM4 模式中恢复，从而触发一次复位。
 * 该项目还展示了如何使用 BURAM 保持寄存器来实现在复位之间保持数据。
 *******************************************************************************/
#include "em_device.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_burtc.h"
#include "em_rmu.h"
#include "bsp.h"
#include "retargetserial.h"
#include "stdio.h"
#include "mx25flash_spi.h"

// BURTC 中断间隔的 1 KHz ULFRCO 时钟数
#define BURTC_IRQ_PERIOD  3000

/**************************************************************************//**
 * @brief  BURTC 中断处理程序
 *****************************************************************************/
void BURTC_IRQHandler(void)
{
  BURTC_IntClear(BURTC_IF_COMP); // 清除比较匹配中断
  GPIO_PinOutToggle(BSP_GPIO_LED0_PORT, BSP_GPIO_LED0_PIN);
}

/**************************************************************************//**
 * @brief  初始化用于按钮和 LED 的 GPIO
 *****************************************************************************/
void initGPIO(void)
{
  GPIO_PinModeSet(BSP_GPIO_PB0_PORT, BSP_GPIO_PB0_PIN, gpioModeInput, 1);
  GPIO_PinModeSet(BSP_GPIO_LED0_PORT, BSP_GPIO_LED0_PIN, gpioModePushPull, 1); // LED 亮
}

/**************************************************************************//**
 * @brief  配置 BURTC 以每隔 BURTC_IRQ_PERIOD 中断一次，并从 EM4 模式唤醒
 *****************************************************************************/
void initBURTC(void)
{
  CMU_ClockSelectSet(cmuClock_EM4GRPACLK, cmuSelect_ULFRCO);
  CMU_ClockEnable(cmuClock_BURTC, true);
  CMU_ClockEnable(cmuClock_BURAM, true);

  BURTC_Init_TypeDef burtcInit = BURTC_INIT_DEFAULT;
  burtcInit.compare0Top = true; // 当计数器达到比较值时重置计数器
  burtcInit.em4comp = true;     // BURTC 比较中断唤醒 EM4 模式（导致复位）
  BURTC_Init(&burtcInit);

  BURTC_CounterReset();
  BURTC_CompareSet(0, BURTC_IRQ_PERIOD);

  BURTC_IntEnable(BURTC_IEN_COMP);    // 比较匹配
  NVIC_EnableIRQ(BURTC_IRQn);
  BURTC_Enable(true);
}

/**************************************************************************//**
 * @brief 检查 RSTCAUSE 获取 EM4 唤醒（复位）的原因，并将唤醒计数保存到 BURAM
 *****************************************************************************/
void checkResetCause (void)
{
  uint32_t cause = RMU_ResetCauseGet();
  RMU_ResetCauseClear();

  // 打印复位原因
  if (cause & EMU_RSTCAUSE_PIN)
  {
    printf("-- RSTCAUSE = PIN \n");
    BURAM->RET[0].REG = 0; // 复位 EM4 唤醒计数
  }
  else if (cause & EMU_RSTCAUSE_EM4)
  {
    printf("-- RSTCAUSE = EM4 唤醒 \n");
    BURAM->RET[0].REG += 1; // 增加 EM4 唤醒计数
  }

  // 打印 EM4 唤醒次数
  printf("-- EM4 唤醒次数 = %ld \n", BURAM->RET[0].REG);
  printf("-- BURTC ISR 将每 ~3 秒切换一次 LED \n");
}

/**************************************************************************//**
 * @brief 主函数
 *****************************************************************************/
int main(void)
{
  CHIP_Init();
  EMU_UnlatchPinRetention();

  // 初始化并关闭 MX25 SPI 闪存
  FlashStatus status;
  MX25_init();
  MX25_RSTEN();
  MX25_RST(&status);
  MX25_DP();
  MX25_deinit();

  // 初始化
  RETARGET_SerialInit();
  RETARGET_SerialCrLf(1);

  printf("在 EM0 中 \n");
  initGPIO();
  initBURTC();
  EMU_EM4Init_TypeDef em4Init = EMU_EM4INIT_DEFAULT;
  EMU_EM4Init(&em4Init);

  // 检查 RESETCAUSE，更新并打印 EM4 唤醒计数
  checkResetCause();

  // 等待用户按下 PB0，重置 BURTC 计数器
  printf("按下 PB0 进入 EM4 \n");
  while(GPIO_PinInGet(BSP_GPIO_PB0_PORT, BSP_GPIO_PB0_PIN) == 1);
  printf("-- 按钮已按下 \n");
  BURTC_CounterReset(); // 重置 BURTC 计数器以等待完整的 ~3 秒在 EM4 唤醒之前
  printf("-- 重置 BURTC 计数器 \n");

  // 进入 EM4
  printf("进入 EM4 并在 ~3 秒后通过 BURTC 比较唤醒 \n\n");
  RETARGET_SerialFlush(); // 等待 printf 完成
  EMU_EnterEM4();

  // 不应该到达这一行
  while(1);
}
