/*
 * retarget.h
 *
 *  Created on: Oct 2, 2025
 *      Author: Daruin Solano
 */

#ifndef RETARGET_H_
#define RETARGET_H_

#include <stdio.h>
#include "log.h"
#include "msg.h"

// Choose your output method: SEMIHOSTING, SWO, or UART
#define USE_SEMIHOSTING   0
#define USE_SWO           0
#define USE_UART          1   // <-- flip this to change output
#define USE_USB			0

void RetargetInit(void);


#endif /* RETARGET_H_ */
