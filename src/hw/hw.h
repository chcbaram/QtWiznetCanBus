/*
 * hw.h
 *
 *  Created on: Nov 12, 2021
 *      Author: baram
 */

#ifndef SRC_HW_HW_H_
#define SRC_HW_HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "can.h"
#include "qbuffer.h"
#include "cmd_can.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_HW_H_ */
