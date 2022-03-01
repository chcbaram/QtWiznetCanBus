/*
 * hw_def.h
 *
 *  Created on: Nov 12, 2021
 *      Author: baram
 */

#ifndef SRC_HW_HW_DEF_H_
#define SRC_HW_HW_DEF_H_


#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>


#include "def.h"
#include "bsp.h"


#define _USE_HW_CMD_CAN
#define      HW_CMD_CAN_MAX_DATA_LENGTH 1024

#define _USE_HW_CAN
#define      HW_CAN_MAX_CH          2
#define      HW_CAN_MSG_RX_BUF_MAX  32


#endif /* SRC_HW_HW_DEF_H_ */
