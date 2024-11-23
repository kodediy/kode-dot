/**
 * @file ctp_hynitron_iic.h
 * @descripttion: 
 * @author: GY
 * @date: 2020-08-18 18:08:04
 *
 * @copyright Copyright (c) 2020
 *
 */
#ifndef CTP_HYNITRON_IIC_H__
#define CTP_HYNITRON_IIC_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define CTP_TRUE    true
#define CTP_FALSE   false

typedef uint8_t     kal_uint8;
typedef uint16_t    kal_uint16;
typedef uint32_t    kal_uint32;
typedef bool        kal_bool;
typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;

#define CST8XX_I2C_SLAVEADDR    0x15    //IIC 7 bit address
#define CST8XX_I2C_UPDATEADDR   0x6A    //IC update address

extern void hctp_reset_ic(void);
extern void hctp_delay_ms(kal_uint16 i);
extern void hctp_i2c_init(kal_uint8 slave_addr,kal_uint16 iic_speed_k);
extern kal_bool hctp_write_bytes(kal_uint16 reg,kal_uint8 *data, kal_uint16 len,kal_uint8 regLen);
extern kal_bool hctp_read_bytes(kal_uint16 reg, kal_uint8 *value, kal_uint16 len,kal_uint8 regLen);

kal_bool ctp_hynitron_cst8_init(void);
kal_bool ctp_hynitron_cst8_get_data(kal_uint16 *xpos, kal_uint16 *ypos);
void ctp_hynitron_cst8_test(void);

#define msleep  hctp_delay_ms
#define mdelay  hctp_delay_ms

#endif /*CTP_HYNITRON_IIC_H__*/


