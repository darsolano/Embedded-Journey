/*
 * retarget.c
 *
 *  Created on: Oct 2, 2025
 *      Author: Daruin Solano
 */

#include "retarget.h"


// UART handle (if using UART output)
#ifndef USE_FULL_LL_DRIVER
#include "stm32l4xx_hal.h"
extern UART_HandleTypeDef huart1;
#elif defined(USE_FULL_LL_DRIVER)
#include "stm32l4xx_ll_usart.h"
#endif

#if USE_SEMIHOSTING
#include <sys/stat.h>
extern void initialise_monitor_handles(void);
#endif

#if USE_SEMIHOSTING || USE_SWO || USE_UART

int __io_getchar(void){
#if USE_SEMIHOSTING
    return fgetc(ch, stdout);

#elif USE_SWO
    return ITM_ReceiveChar();

#elif USE_UART
#ifndef USE_FULL_LL_DRIVER
    uint8_t ch = 0;
    HAL_UART_Receive(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
#elif defined(USE_FULL_LL_DRIVER)
	while(!LL_USART_IsActiveFlag_RXNE(USART1));
    return LL_USART_ReceiveData8(USART1);
#endif

#endif
}

int __io_putchar(int ch)
{
#if USE_SEMIHOSTING
    return fputc(ch, stdout);

#elif USE_SWO
    ITM_SendChar(ch);
    return ch;

#elif USE_UART
#ifndef USE_FULL_LL_DRIVER
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return 1;
#elif defined(USE_FULL_LL_DRIVER)
	while(!LL_USART_IsActiveFlag_TXE(USART1));
    LL_USART_TransmitData8(USART1, ch);
#endif

#endif
}
#endif

#if USE_USB
#include "usbd_cdc_if.h"
// newlib "_write" override — routes stdout to USB
int _write(int file, char *ptr, int len)
{
    uint8_t result = CDC_Transmit_FS((uint8_t*)ptr, len);

    // CDC_Transmit_FS returns USBD_BUSY if previous transfer not done.
    // You can retry or block until ready:
    while (result == USBD_BUSY)
    {
        result = CDC_Transmit_FS((uint8_t*)ptr, len);
    }

    return len;
}
#endif

void RetargetInit(void)
{
#if USE_SEMIHOSTING
    initialise_monitor_handles();   // enable semihosting
#elif USE_SWO
    // SWO doesn’t need init; just make sure debug is set up
#elif USE_UART
    // UART already initialized by CubeMX
#endif
}


