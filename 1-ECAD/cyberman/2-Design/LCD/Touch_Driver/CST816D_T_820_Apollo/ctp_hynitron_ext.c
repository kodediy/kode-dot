/**
 *Name        : ctp_hynitron_ext.c
 *Author      : gary
 *Version     : V1.0
 *Create      : 2017-11-23
 *Copyright   : zxzz
 */

#include "capacitive_tp_hynitron_cst8xx.h"
#include "ctp_hynitron_iic.h"
#include "ctp_hynitron_ext.h"

#if CTP_HYNITRON_EXT==1
#define REG_LEN_1B  1
#define REG_LEN_2B  2

#if CTP_HYNITRON_EXT_CST816T_UPDATE==1
/*****************************************************************/
// For CSK0xx update
 /*
  *
  */
static int cst816t_enter_bootmode(void){
     char retryCnt = 10;

     hctp_reset_ic();  // reset the tp ic

     while(retryCnt--){
         u8 cmd[3];
         cmd[0] = 0xAB;
         if (CTP_FALSE == hctp_write_bytes(0xA001,cmd,1,REG_LEN_2B)){  // enter program mode
             mdelay(2); // 4ms
             continue;                   
         }
         if (CTP_FALSE == hctp_read_bytes(0xA003,cmd,1,REG_LEN_2B)) { // read flag
             mdelay(2); // 4ms
             continue;                           
         }else{
             if (cmd[0] != 0xC1){
                 msleep(2); // 4ms
                 continue;
             }else{
                 return 0;
             }
         }
     }
     return -1;
 }
 /*
  *
  */
static int cst816t_update(u16 startAddr,u16 len,u8* src,u16 delay_ms){
     u16 sum_len;
     u8 cmd[10];

     if (cst816t_enter_bootmode() == -1){
        return -1;
     }
     sum_len = 0;
 
#define PER_LEN	512
     do{
         if (sum_len >= len){
             return -1;
         }
         
         // send address
         cmd[0] = startAddr&0xFF;
         cmd[1] = startAddr>>8;
         hctp_write_bytes(0xA014,cmd,2,REG_LEN_2B);
         
//         for(int i = 0; i < 4; i++)
//            hctp_write_bytes(0xA018 + 128 * i, src + 128 * i, PER_LEN / 4, REG_LEN_2B);
       {
        u8 temp_buf[8];
		u16 j,iic_addr;
		iic_addr=0;
        for(j=0; j<128; j++){
			
	    	temp_buf[0] = *((u8*)src+iic_addr+0);
	    	temp_buf[1] = *((u8*)src+iic_addr+1);
			temp_buf[2] = *((u8*)src+iic_addr+2);
			temp_buf[3] = *((u8*)src+iic_addr+3);

	    	hctp_write_bytes((0xA018+iic_addr),(u8* )temp_buf,4,REG_LEN_2B);
			iic_addr+=4;
			if(iic_addr==512) break;
		}
	   }
         cmd[0] = 0xEE;
         hctp_write_bytes(0xA004,cmd,1,REG_LEN_2B);
         msleep(delay_ms);
 
         {
             u8 retrycnt = 50;
             while(retrycnt--){
                 cmd[0] = 0;
                 hctp_read_bytes(0xA005,cmd,1,REG_LEN_2B);
                 if (cmd[0] == 0x55){
                     // success
                     break;
                 }
                 msleep(10);
             }
         }
         startAddr += PER_LEN;
         src += PER_LEN;
         sum_len += PER_LEN;
     }while(len);
     
     // exit program mode
     cmd[0] = 0x00;
     hctp_write_bytes(0xA003,cmd,1,REG_LEN_2B);
     return 0;
 }
 /*
  *
  */
static u32 cst816t_read_checksum(u16 startAddr,u16 len){
     union{
         u32 sum;
         u8 buf[4];
     }checksum;
     char cmd[3];
     char readback[4] = {0};
 
     if (cst816t_enter_bootmode() == -1){
        return -1;
     }
     
     cmd[0] = 0;
     if (CTP_FALSE == hctp_write_bytes(0xA003,cmd,1,REG_LEN_2B)){
         return -1;
     }
     msleep(500);

     checksum.sum = 0;
     if (CTP_FALSE == hctp_read_bytes(0xA008,checksum.buf,2,REG_LEN_2B)){
         return -1;
     }
     return checksum.sum;
}
#endif 

/*****************************************************************/
// common

 /*
  *
  */
 kal_bool ctp_hynitron_update(void)
 {
     kal_uint8 lvalue;
     kal_uint8 write_data[2];
     kal_bool temp_result = CTP_TRUE;

#if CTP_HYNITRON_EXT_CST816T_UPDATE==1
    hctp_i2c_init(CST8XX_I2C_UPDATEADDR, 50);     
    if (cst816t_enter_bootmode() == 0){
#include "capacitive_hynitron_cst8xx_update.h"
        if(sizeof(app_bin) > 10){
            kal_uint16 startAddr = app_bin[1];
            kal_uint16 length = app_bin[3];
            kal_uint16 checksum = app_bin[5];
            startAddr <<= 8; startAddr |= app_bin[0];
            length <<= 8; length |= app_bin[2];
            checksum <<= 8; checksum |= app_bin[4];
			if(startAddr == 0x0000 && length == 0x3C00){
            	if(cst816t_read_checksum(startAddr, length) != checksum){
                	cst816t_update(startAddr, length, (uint8_t *)app_bin+6,100);
                	if(cst816t_read_checksum(startAddr, length) != checksum){
						cst816t_update(startAddr, length, (uint8_t *)app_bin+6,300);
						cst816t_read_checksum(startAddr, length);
                	}					
            	}
			}
        }
        return CTP_TRUE;
    }
#endif

      return CTP_FALSE;
 }

#endif  //CTP_HYNITRON_EXT==1
