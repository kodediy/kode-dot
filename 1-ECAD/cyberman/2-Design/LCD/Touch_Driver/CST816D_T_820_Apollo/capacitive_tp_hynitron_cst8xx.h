/**
 * @file capacitive_tp_hynitron_cst8xx.h
 * @descripttion: 
 * @author: GY
 * @date: 2020-08-18 18:18:23
 *
 * @copyright Copyright (c) 2020
 *
 */
#ifndef CAPACITIVE_TP_HYNITRON_CST8XX_H__
#define CAPACITIVE_TP_HYNITRON_CST8XX_H__


#define CTP_HYNITRON_EXT                1 //enable update
#define CTP_HYNITRON_EXT_CST816T_UPDATE 1

#define CTP_RESET_PIN                   49 //IC reset pin
#define CTP_SET_RESET_PIN_OUTPUT
#define CTP_SET_RESET_PIN_HIGH
#define CTP_SET_RESET_PIN_LOW 

kal_bool ctp_hynitron_cst8_init(void);
kal_bool ctp_hynitron_cst8_power_on(kal_bool enable);
kal_bool ctp_hynitron_cst8_gesture_wake_up(void);
kal_bool ctp_hynitron_cst8_get_data(kal_uint16 *xpos, kal_uint16 *ypos);

#endif /*CAPACITIVE_TP_HYNITRON_CST8XX_H__*/
