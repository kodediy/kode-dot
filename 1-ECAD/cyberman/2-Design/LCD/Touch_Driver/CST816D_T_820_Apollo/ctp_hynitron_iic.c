/**
 * @file ctp_hynitron_iic.c
 * @descripttion: cst8xx iic driver
 * @author: GY
 * @date: 2020-08-18 18:00:27
 *
 * @copyright Copyright (c) 2020
 *
 */
#include "am_util.h"
#include "FreeRTOS.h"
#include "capacitive_tp_hynitron_cst8xx.h"
#include "ctp_hynitron_iic.h"
#include "TpIicDriver.h"


/*
*************************************************************************
*                              函数定义
*************************************************************************
*/

/**
 * @brief   cst8xx 硬件复位
 *
 */
void hctp_reset_ic(void)
{
    am_hal_gpio_pinconfig(CTP_RESET_PIN, g_AM_HAL_GPIO_OUTPUT_4);
    am_hal_gpio_output_clear(CTP_RESET_PIN);
    hctp_delay_ms(20);
    am_hal_gpio_output_set(CTP_RESET_PIN);
    hctp_delay_ms(10);
}

/**
 * @brief   cst8xx ms延时函数
 *
 * @param   毫秒
 *
 */
void hctp_delay_ms(kal_uint16 i)
{
    am_util_delay_ms(i);
}

/**
 * @brief   cst8xx iic配置函数
 *
 * @param   slave_addr IIC 7bit地址
 * @param   iic_speed_k IIC 通信速率
 *
 * @return
 */
void hctp_i2c_init(kal_uint8 slave_addr, kal_uint16 iic_speed_k)
{
    TpIicSetSlaveAddr(slave_addr);
}

/**
 * @brief   cst8xx iic写接口
 *
 * @param   reg 寄存器地址
 * @param   data 写入数据
 * @param   len 数据长度
 * @param   regLen 寄存器地址长度
 *
 * @return  CTP_TRUE成功
 */
kal_bool hctp_write_bytes(kal_uint16 reg, kal_uint8 *data, kal_uint16 len, kal_uint8 regLen)
{
    uint32_t u32Status = AM_HAL_STATUS_SUCCESS;

    uint8_t * tx_data = (uint8_t *)pvPortMalloc(len + regLen);

    if (regLen == sizeof(kal_uint8))
    {
        tx_data[0] = (uint8_t)reg;
        for (int i = 0; i < len; i++)
            tx_data[1 + i] = data[i];
    }
    else
    {
        tx_data[0] = reg >> 8;      //寄存器高位
        tx_data[1] = reg & 0xff;    //寄存器低位
        for (int i = 0; i < len; i++)
            tx_data[2 + i] = data[i];
    }

    u32Status = _inTpIomIicTxBlocking((uint8_t *)tx_data, len + regLen);

    vPortFree(tx_data);

    return (u32Status == AM_HAL_STATUS_SUCCESS) ? CTP_TRUE : CTP_FALSE;
}

/**
 * @brief   cst8xx iic读接口
 *
 * @param   reg 寄存器地址
 * @param   data 读出数据地址
 * @param   len 数据长度
 * @param   regLen 寄存器地址长度
 *
 * @return  CTP_TRUE成功
 */
kal_bool hctp_read_bytes(kal_uint16 reg, kal_uint8 *value, kal_uint16 len, kal_uint8 regLen)
{
    uint32_t u32Status = AM_HAL_STATUS_SUCCESS;
    if (regLen == sizeof(kal_uint8))
    {
        u32Status = _inTpIomIicRxBlocking8RegAddr(reg, value, len);
    }
    else
    {
        uint16_t _reg16 = ((reg >> 8) & 0xFF) | ((reg << 8) & 0xFF00);
        u32Status = _inTpIomIicRxBlocking(_reg16, value, len);
    }
    return (u32Status == AM_HAL_STATUS_SUCCESS) ? CTP_TRUE : CTP_FALSE;
}
