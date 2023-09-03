/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "stm32_hal_ll.h"
#include "stm32_hal.h"
#include "stm32_gpio.h"

#include "hal/adc_driver.h"
#include "hal/trainer_driver.h"
#include "hal/switch_driver.h"
#include "hal/module_port.h"
#include "hal/gpio.h"

#include "board.h"
#include "boards/generic_stm32/module_ports.h"
#include "boards/generic_stm32/intmodule_heartbeat.h"
#include "boards/generic_stm32/analog_inputs.h"

#include "debug.h"
#include "rtc.h"

#include "timers_driver.h"
#include "dataconstants.h"

#if defined(FLYSKY_GIMBAL)
  #include "flysky_gimbal_driver.h"
#endif

#if !defined(BOOT)
  #include "opentx.h"
  #if defined(PXX1)
    #include "pulses/pxx1.h"
  #endif
#endif

#if defined(BLUETOOTH)
  #include "bluetooth_driver.h"
#endif

#if defined(__cplusplus)
extern "C" {
#endif
#include "usb_dcd_int.h"
#include "usb_bsp.h"
#if defined(__cplusplus)
}
#endif

#if !defined(BOOT)
bool UNEXPECTED_SHUTDOWN()
{
  return WAS_RESET_BY_WATCHDOG()
    || g_eeGeneral.unexpectedShutdown;
}
#endif

HardwareOptions hardwareOptions;

void watchdogInit(unsigned int duration)
{
  IWDG->KR = 0x5555;      // Unlock registers
  IWDG->PR = 3;           // Divide by 32 => 1kHz clock
  IWDG->KR = 0x5555;      // Unlock registers
  IWDG->RLR = duration;
  IWDG->KR = 0xAAAA;      // reload
  IWDG->KR = 0xCCCC;      // start
}

#if !defined(BOOT)

#if defined(FUNCTION_SWITCHES)
#include "storage/storage.h"
#endif

void boardInit()
{
  LL_APB1_GRP1_EnableClock(AUDIO_RCC_APB1Periph);
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

#if defined(BLUETOOTH) && !defined(PCBX9E)
  bluetoothInit(BLUETOOTH_DEFAULT_BAUDRATE, true);
#endif

#if defined(MANUFACTURER_RADIOMASTER) && defined(STM32F407xx)
  FLASH_OBProgramInitTypeDef OBInit;
  HAL_FLASHEx_OBGetConfig(&OBInit);
  if (OBInit.BORLevel != OB_BOR_LEVEL3) {
    OBInit.OptionType = OPTIONBYTE_BOR;
    OBInit.BORLevel = OB_BOR_LEVEL3;
    HAL_FLASH_OB_Unlock();
    HAL_FLASHEx_OBProgram(&OBInit);
    HAL_FLASH_OB_Launch();
    HAL_FLASH_OB_Lock();
  }
#endif

  init_trainer();
  // Sets 'hardwareOption.pcbrev' as well
  pwrInit();
  boardInitModulePorts();

#if defined(INTERNAL_MODULE_PXX1) && defined(PXX_FREQUENCY_HIGH)
  pxx1SetInternalBaudrate(PXX1_FAST_SERIAL_BAUDRATE);
#endif

#if defined(INTMODULE_HEARTBEAT) &&                                     \
  (defined(INTERNAL_MODULE_PXX1) || defined(INTERNAL_MODULE_PXX2))
  pulsesSetModuleInitCb(_intmodule_heartbeat_init);
  pulsesSetModuleDeInitCb(_intmodule_heartbeat_deinit);
#endif
  
// #if defined(AUTOUPDATE)
//   telemetryPortInit(FRSKY_SPORT_BAUDRATE, TELEMETRY_SERIAL_WITHOUT_DMA);
//   sportSendByteLoop(0x7E);
// #endif

#if defined(STATUS_LEDS)
  ledInit();
#if defined(MANUFACTURER_RADIOMASTER) || defined(MANUFACTURER_JUMPER) || defined(RADIO_COMMANDO8)
  ledBlue();
#else
  ledGreen();
#endif
#endif

// Support for FS Led to indicate battery charge level
#if defined(FUNCTION_SWITCHES)
  // This is needed to prevent radio from starting when usb is plugged to charge
  usbInit();
  // prime debounce state...
   usbPlugged();

   if (usbPlugged()) {
     delaysInit();
     adcInit(&_adc_driver);
     getADC();
     pwrOn(); // required to get bat adc reads
     INTERNAL_MODULE_OFF();
     EXTERNAL_MODULE_OFF();

     while (usbPlugged()) {
       // Let it charge ...
       getADC(); // Warning: the value read does not include VBAT calibration
       delay_ms(20);
       if (getBatteryVoltage() >= 660)
         fsLedOn(0);
       if (getBatteryVoltage() >= 700)
         fsLedOn(1);
       if (getBatteryVoltage() >= 740)
         fsLedOn(2);
       if (getBatteryVoltage() >= 780)
         fsLedOn(3);
       if (getBatteryVoltage() >= 820)
         fsLedOn(4);
       if (getBatteryVoltage() >= 842)
         fsLedOn(5);
     }
     pwrOff();
   }
#endif

  keysInit();
  switchInit();

#if defined(ROTARY_ENCODER_NAVIGATION)
  rotaryEncoderInit();
#endif

  delaysInit();
  __enable_irq();

#if defined(PWM_STICKS)
  sticksPwmDetect();
#endif

#if defined(FLYSKY_GIMBAL)
  flysky_gimbal_init();
#endif

  if (!adcInit(&_adc_driver))
    TRACE("adcInit failed");

  lcdInit(); // delaysInit() must be called before
  audioInit();
  init2MhzTimer();
  init1msTimer();
  usbInit();

#if defined(DEBUG)
  serialInit(SP_AUX1, UART_MODE_DEBUG);
#endif

#if defined(HAPTIC)
  hapticInit();
#endif

#if defined(PXX2_PROBE)
  intmodulePxx2Probe();
#endif

#if defined(DEBUG)
  // Freeze timers & watchdog when core is halted
  DBGMCU->APB1FZ = 0x00E009FF;
  DBGMCU->APB2FZ = 0x00070003;
#endif

#if defined(PWR_BUTTON_PRESS)
  if (WAS_RESET_BY_WATCHDOG_OR_SOFTWARE()) {
    pwrOn();
  }
#endif

#if defined(TOPLCD_GPIO)
  toplcdInit();
#endif

#if defined(USB_CHARGER)
  usbChargerInit();
#endif

#if defined(JACK_DETECT_GPIO)
  initJackDetect();
#endif

  initSpeakerEnable();
  enableSpeaker();

  initHeadphoneTrainerSwitch();

#if defined(RTCLOCK)
  rtcInit(); // RTC must be initialized before rambackupRestore() is called
#endif

  backlightInit();

#if defined(GUI)
  lcdSetContrast(true);
#endif
}
#endif

void boardOff()
{
#if defined(STATUS_LEDS) && !defined(BOOT)
  ledOff();
#endif

  BACKLIGHT_DISABLE();

#if defined(TOPLCD_GPIO) && !defined(BOOT)
  toplcdOff();
#endif

#if defined(PWR_BUTTON_PRESS)
  while (pwrPressed()) {
    WDG_RESET();
  }
#endif

#if defined(MANUFACTURER_RADIOMASTER) && defined(STM32F407xx)
  lcdInit(); 
#endif

  lcdOff();
  SysTick->CTRL = 0; // turn off systick
  pwrOff();

  // disable interrupts
  __disable_irq();

  while (1) {
    WDG_RESET();
#if defined(PWR_BUTTON_PRESS)
    // X9E/X7 needs watchdog reset because CPU is still running while
    // the power key is held pressed by the user.
    // The power key should be released by now, but we must make sure
    if (!pwrPressed()) {
      // Put the CPU into sleep to reduce the consumption,
      // it might help with the RTC reset issue
      PWR->CR |= PWR_CR_CWUF;
      /* Select STANDBY mode */
      PWR->CR |= PWR_CR_PDDS;
      /* Set SLEEPDEEP bit of Cortex System Control Register */
      SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
      /* Request Wait For Event */
      __WFE();
    }
#endif
  }

  // this function must not return!
}

#if defined(AUDIO_SPEAKER_ENABLE_GPIO)
void initSpeakerEnable()
{
  gpio_init(AUDIO_SPEAKER_ENABLE_GPIO, GPIO_OUT);
}

void enableSpeaker()
{
  gpio_set(AUDIO_SPEAKER_ENABLE_GPIO);
}

void disableSpeaker()
{
  gpio_clear(AUDIO_SPEAKER_ENABLE_GPIO);
}
#endif

#if defined(HEADPHONE_TRAINER_SWITCH_GPIO)
void initHeadphoneTrainerSwitch()
{
  gpio_init(HEADPHONE_TRAINER_SWITCH_GPIO, GPIO_OUT);
}

void enableHeadphone()
{
  gpio_clear(HEADPHONE_TRAINER_SWITCH_GPIO);
}

void enableTrainer()
{
  gpio_set(HEADPHONE_TRAINER_SWITCH_GPIO);
}
#endif

#if defined(JACK_DETECT_GPIO)
void initJackDetect(void)
{
  gpio_init(JACK_DETECT_GPIO, GPIO_IN_PU);
}

bool isJackPlugged()
{
  // debounce
  static bool debounced_state = 0;
  static bool last_state = 0;

  if (gpio_read(JACK_DETECT_GPIO)) {
    if (!last_state) {
      debounced_state = false;
    }
    last_state = false;
  }
  else {
    if (last_state) {
      debounced_state = true;
    }
    last_state = true;
  }
  return debounced_state;
}
#endif
