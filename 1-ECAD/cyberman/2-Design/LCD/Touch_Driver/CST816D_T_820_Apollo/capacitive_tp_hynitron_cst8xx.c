/**
 * @file capacitive_tp_hynitron_cst8xx.c
 * @descripttion:
 * @author: GY
 * @date: 2020-08-18 18:22:53
 *
 * @copyright Copyright (c) 2020
 *
 */

#include "capacitive_tp_hynitron_cst8xx.h"
#include "ctp_hynitron_iic.h"
#include "ctp_hynitron_ext.h"
#include "TpIicDriver.h"
#include "SEGGER_RTT.h"

/*
*************************************************************************
*                              宏定义
*************************************************************************
*/

#define DBG_CTP

#ifdef DBG_CTP
    #define ctp_dbg_print(...)  _SEGGER_RTT_printf(__VA_ARGS__)
#else
    #define ctp_dbg_print(...)
#endif


/**
 * @brief   cst816t 写指令后再回读判断，防止出错
 *
 */
kal_uint8 write_reg_and_check(kal_uint8 reg,kal_uint8 cmd)
{
    kal_uint8 buff = 0;
    kal_uint8 retry = 5;
    for(;retry;retry--){
        hctp_write_bytes(reg, &cmd, 1, 1);
        hctp_read_bytes(reg, &buff, 1, 1);     
        if(buff == cmd)  break;    
    }
    if(0 == retry)return CTP_FALSE;
    return CTP_TRUE;
}

/**
 * @brief   cst816t 读项目信息，防止出错
 *
 */
kal_uint8 read_HYN_message_and_check(kal_uint8 reg, kal_uint8 *value)
{
    kal_uint8 buff[3] = {0,1,2};
    kal_uint8 retry = 5;
    kal_uint8 i = 0;
    for(;retry;retry--){
        for(i = 0; i < 3; i++){
            hctp_read_bytes(reg, &(buff[i]), 1, 1);       
        }   

        if((buff[0] == buff[1])&&(buff[1] == buff[2])&&(buff[0] == buff[2])){
            *value = buff[0];
            break;    
        }  
    }

    if(0 == retry)return CTP_FALSE;
    return CTP_TRUE;
}




/**
 * @brief   cst816t 初始化函数
 *
 */
kal_bool ctp_hynitron_cst8_init(void)
{
#if CTP_HYNITRON_EXT == 1
    ctp_hynitron_update();
#endif
    hctp_i2c_init(CST8XX_I2C_SLAVEADDR, 300);

    hctp_reset_ic();

    hctp_delay_ms(150);

    kal_uint8 lvalue;
    //hctp_read_bytes(0xA9, &lvalue, 1, 1);
    read_HYN_message_and_check(0xA9, &lvalue);
    // read_HYN_message_and_check(0xA7, &lvalue);//ChipID 芯片型号
    // read_HYN_message_and_check(0xA8, &lvalue);//ProjID 工程编号
    // read_HYN_message_and_check(0xAA, &lvalue);//FactoryID TP厂家ID

    /**读取TP版本号*/
	extern uint32_t m_tp_cur_fw;
	extern uint32_t m_tp_cur_cfg;

    m_tp_cur_fw = lvalue;
    m_tp_cur_cfg = lvalue;

    return CTP_TRUE;
}

/**
 * @brief   cst816t 设置电源模式
 *
 */
kal_bool ctp_hynitron_cst8_power_on(kal_bool enable)
{
    //_TODO:  Implement this funciton by customer
    if (enable)
    {
        CTP_SET_RESET_PIN_LOW;
        hctp_delay_ms(10);
        CTP_SET_RESET_PIN_HIGH;
    }
    else
    {
        write_reg_and_check(0xE5,0x03);
    }
    return CTP_TRUE;
}

/**
 * @brief   cst816t 开启手势唤醒功能
 *
 */
kal_bool ctp_hynitron_cst8_gesture_wake_up(void)
{
    CTP_SET_RESET_PIN_LOW;
    hctp_delay_ms(10);
    CTP_SET_RESET_PIN_HIGH;

    hctp_delay_ms(100);
    
    write_reg_and_check(0xFE,0x00);
    write_reg_and_check(0xE5,0x01);
    
    return CTP_TRUE;
}


/*
This function is used to get the raw data of the fingures that are pressed.
When CTP IC send intterupt signal to BB chip, this function will be called in the interrupt handler function.

ATTENTION: Becasue this function is called in the interrupt handler function, it MUST NOT run too long.
That will block the entire system.
If blocking too long, it generally will cause system crash *....*
*/
kal_bool ctp_hynitron_cst8_get_data(kal_uint16 *xpos, kal_uint16 *ypos)
{
    kal_bool temp_result;
    kal_uint8 lvalue[5];
    kal_uint32 counter = 0;
    kal_uint32 model = 0;

    kal_bool ret = hctp_read_bytes(0x02, lvalue, 5, 1);

    if(ret == CTP_FALSE)
    {
        ctp_dbg_print("hctp_read_bytes error\n");
    }

    model = lvalue[0];
    ctp_dbg_print("ctp_get_data finger_num = %d\n", model);

    /*
    0 fingure meas UP EVENT, so return FALSE;
    And now we only support FIVE fingures at most, so if more than 5 fingures also return FALSE
    */
    if ((model == 0) || (model > 2))
    {
        return CTP_FALSE;
    }

    *xpos = (((kal_uint16)(lvalue[1] & 0x0f)) << 8) | lvalue[2];;
    *ypos = (((kal_uint16)(lvalue[3] & 0x0f)) << 8) | lvalue[4];
    ctp_dbg_print("piont[%d], x:%d, y:%d\n", 0, *xpos, *ypos);
    return CTP_TRUE;
}

/**
 * @brief   cst816t 读点测试
 *
 */
void ctp_hynitron_cst8_test(void)
{
    TpHalInit();
    ctp_hynitron_cst8_init();

    uint16_t xpos = 0, ypos = 0;
    while(1)
    {
        ctp_hynitron_cst8_get_data(&xpos, &ypos);
        vTaskDelay(50);
        ctp_dbg_print("xpos = %d, ypos = %d\n", xpos, ypos);
    }
}
