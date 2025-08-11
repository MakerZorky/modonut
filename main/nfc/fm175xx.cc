/*************************************************************/
//2014.07.15????
/*************************************************************/
//#include "includeall.h"
#include "FM175XX.h"
#include "spi.h"
#include "Mifare_005M.h"
#include "esp_system.h"
#include "esp_log.h"

unsigned char PICC_ATQA[2]={0},PICC_UID[10]={0};

#define Fm175xx_Debug 0
/*************************************************************/

/*************************************************************/
unsigned char Read_Reg(unsigned char reg_add)
{
unsigned char reg_value;
	reg_value=SPIRead(reg_add);
 return reg_value;
}
/*************************************************************/

/*************************************************************/
//unsigned char Read_Reg_All(unsigned char *reg_value)
//{
//unsigned char data i;
//	for (i=0;i<64;i++)	   
//		*(reg_value+i)=Read_Reg(i);
// return OK;
//}
/*************************************************************/

/*************************************************************/
unsigned char Write_Reg(unsigned char reg_add,unsigned char reg_value)
{
	SPIWrite(reg_add,reg_value);
	return OK;
}


/*************************************************************/

/*************************************************************/
/*
void ModifyReg(unsigned char reg_add,unsigned char mask,unsigned char set)
{
	unsigned char reg_value;
	reg_value = Read_Reg(reg_add);
	if(set)
	{
		reg_value |= mask;
	}
	else
	{
		reg_value &= ~mask;
	}
	Write_Reg(reg_add,reg_value);
	return ;
}
*/
/*************************************************************/

/*************************************************************/
void Read_FIFO(unsigned char length,unsigned char *fifo_data)
{
  unsigned char i;
	for(i=0;i<length;i++)
	   *(fifo_data+i)=SPIRead(FIFODataReg);	
	//SPIRead_Sequence(length,FIFODataReg,fifo_data);
	return;
}
/*************************************************************/

/*************************************************************/
void Write_FIFO(unsigned char length,unsigned char *fifo_data)
{
  unsigned char i;
	for(i=0;i<length;i++)
	   Write_Reg(FIFODataReg,*(fifo_data+i));		
	//SPIWrite_Sequence(length,FIFODataReg,fifo_data);
	return;
}
/*************************************************************/

/*************************************************************/
unsigned char Clear_FIFO(void)
{
	 Set_BitMask(FIFOLevelReg,0x80);//???FIFO????
	 if(Read_Reg(FIFOLevelReg)==0)
	 	return OK;
	else
		return FM175XX_ERROR;
}
/*************************************************************/

/*************************************************************/
unsigned char Set_BitMask(unsigned char reg_add,unsigned char mask)
{
	unsigned char result;
	result=Write_Reg(reg_add,Read_Reg(reg_add) | mask);  // set bit mask
	return result;
}
/*********************************************/

/*********************************************/
unsigned char Clear_BitMask(unsigned char reg_add,unsigned char mask)
{
	unsigned char result;
	result=Write_Reg(reg_add,Read_Reg(reg_add) & ~mask);  // clear bit mask
	return result;
}
/*********************************************/

/*********************************************/
unsigned char Set_Rf(unsigned char mode)
{
	unsigned char result = 0 ;
	if((Read_Reg(TxControlReg)&0x03)==mode){	
		return OK;
	}
	if(mode==0)
		{
		result=Clear_BitMask(TxControlReg,0x03); //???TX1??TX2????
		}
	if(mode==1)
		{
		result=Clear_BitMask(TxControlReg,0x01); //????TX1????
		}
	if(mode==2)
		{
		result=Clear_BitMask(TxControlReg,0x02); //????TX2????
		}
	if(mode==3)
		{
		result=Set_BitMask(TxControlReg,0x03); //??TX1??TX2????
		}
	// Delay_100us(50);//??TX????????????????????????????
	vTaskDelay(pdMS_TO_TICKS(5));
	return result;
}
/*********************************************/

/*********************************************/  
unsigned char Pcd_Comm(	unsigned char Command, 
                 		unsigned char *pInData, 
                 		unsigned char InLenByte,
                 		unsigned char *pOutData, 
                 		unsigned int *pOutLenBit)
{
    unsigned char result=0;
	unsigned char rx_temp=0;//?????????????
	unsigned char rx_len=0;//??????????????
    unsigned char lastBits=0;//????????��????
    unsigned char irq;
	unsigned long T=0;
	*pOutLenBit =0;
	Clear_FIFO();
    Write_Reg(CommandReg,Idle);
    Write_Reg(WaterLevelReg,0x20);//????FIFOLevel=32???
	Write_Reg(ComIrqReg,0x7F);//???IRQ???
 	if(Command==MFAuthent)
		{
		  Write_FIFO(InLenByte,pInData);//??????????	
		  Set_BitMask(BitFramingReg,0x80);//????????
		}
    Set_BitMask(TModeReg,0x80);//????????????

 	Write_Reg(CommandReg,Command);

	while(1)//????��??��???
		{
	        irq = Read_Reg(ComIrqReg);//????��???		
			if(irq&0x01)	//TimerIRq  ???????????
			{
				result=TIMEOUT_Err;		
				break;
			}
            //if(T==65535*3) //检测周期低于300ms，就会触发看门狗
	        if(T==20000) 
			{			
			  break;
			}
				
			if(Command==MFAuthent)
			{
				if(irq&0x10)	//IdelIRq  command?????????��??????????
				{
					result=OK;
			  		break;
				}
		   	}

			if(Command==Transmit)	
				{
					if((irq&0x04)&&(InLenByte>0))	//LoAlertIrq+?????????????0
						{
							if (InLenByte<32)
								{
									Write_FIFO(InLenByte,pInData);	
									InLenByte=0;
								}
								else
								{
									Write_FIFO(32,pInData);
									InLenByte=InLenByte-32;
									pInData=pInData+32;
								}
							Set_BitMask(BitFramingReg,0x80);	//????????
							Write_Reg(ComIrqReg,0x04);	//???LoAlertIrq
						}	
					
					if((irq&0x40)&&(InLenByte==0))	//TxIRq
					{
						result=OK;
					  	break;
					}
				}
							  
			if(Command==Transceive)
				{
					if((irq&0x04)&&(InLenByte>0))	//LoAlertIrq + ?????????????0
					{	
						if (InLenByte>32)
							{
								Write_FIFO(32,pInData);
								InLenByte=InLenByte-32;
								pInData=pInData+32;
							}
						else
							{
								Write_FIFO(InLenByte,pInData);
								InLenByte=0;
							}
							Set_BitMask(BitFramingReg,0x80);//????????
							Write_Reg(ComIrqReg,0x04);//???LoAlertIrq
					}
					if(irq&0x08)	//HiAlertIRq
					{
						 if((irq&0x40)&&(InLenByte==0)&&(Read_Reg(FIFOLevelReg)>32))//TxIRq	+ ??????????0 + FIFO???????32
						  	{
								Read_FIFO(32,pOutData+rx_len); //????FIFO????
								rx_len=rx_len+32;
								Write_Reg(ComIrqReg,0x08);	//???? HiAlertIRq
							}
						}
				    if((irq&0x20)&&(InLenByte==0))	//RxIRq=1
						{
							result=OK;
					  		break;
						}
				    }
				T++;
		}


        {   
			 if(Command == Transceive)
			 	{
					rx_temp=Read_Reg(FIFOLevelReg);
					if(rx_temp==0xFF)
						return FM175XX_ERROR;
					lastBits = Read_Reg(ControlReg) & 0x07;

					#if Fm175xx_Debug
						Uart_Send_Msg("<- rx_temp = ");Uart_Send_Hex(&rx_temp,1);Uart_Send_Msg("\r\n");
						Uart_Send_Msg("<- rx_len = ");Uart_Send_Hex(&rx_len,1);Uart_Send_Msg("\r\n");
						Uart_Send_Msg("<- lastBits = ");Uart_Send_Hex(&lastBits,1);Uart_Send_Msg("\r\n");
					#endif

					if((rx_temp==0)&&(lastBits>0))//??????????��???1???????????????????1??????
						rx_temp=1;	
					
					Read_FIFO(rx_temp,pOutData+rx_len); //????FIFO????
				   
					rx_len=rx_len+rx_temp;//??????????
					
					#if Fm175xx_Debug
						Uart_Send_Msg("<- FIFO DATA = ");Uart_Send_Hex(pOutData,rx_len);Uart_Send_Msg("\r\n");
					#endif
               
                if(lastBits>0)
	                *pOutLenBit = (rx_len-1)*(unsigned int)8 + lastBits;  
			    else
	                *pOutLenBit = rx_len*(unsigned int)8; 
				
				}
		}
		if(result==OK)
			result=Read_Reg(ErrorReg);//ErrorReg??????Pcd_Comm??????????????????????��?????????
		
		#if Fm175xx_Debug
			reg_data=Read_Reg(ErrorReg);
			Uart_Send_Msg("<- ErrorReg = ");Uart_Send_Hex(&reg_data,1);Uart_Send_Msg("\r\n");
		#endif

    Set_BitMask(ControlReg,0x80);     // stop timer now
    Write_Reg(CommandReg,Idle); 
 	Clear_BitMask(BitFramingReg,0x80);//??????
    return result;
}
/*********************************************/

/*********************************************/
 unsigned char Pcd_SetTimer(unsigned long delaytime)//?څ??????ms??
{
	unsigned long TimeReload;
//	unsigned int data Prescaler;

//	Prescaler=0;
	TimeReload=0;
//	while(Prescaler<0xfff)
//	{
//		TimeReload = ((delaytime*(long)13560)-1)/(Prescaler*2+1);
//		
//		if( TimeReload<0xffff)
//			break;
//		Prescaler++;
//	}
//		TimeReload=TimeReload&0xFFFF;
//		Set_BitMask(TModeReg,Prescaler>>8);
//		Write_Reg(TPrescalerReg,Prescaler&0xFF);					
//		Write_Reg(TReloadMSBReg,TimeReload>>8);
//		Write_Reg(TReloadLSBReg,TimeReload&0xFF);
	//Set_BitMask(TModeReg,0x00);
	
	Write_Reg(TPrescalerReg,0xA9);	
	TimeReload=65535-(delaytime*40);
	Write_Reg(TReloadMSBReg,TimeReload>>8);
	Write_Reg(TReloadLSBReg,TimeReload&0xFF);
	return OK;
}
/*********************************************/

/*********************************************/
unsigned char Pcd_ConfigISOType(unsigned char type)
{
	
   if (type == 0)                     //ISO14443_A
   { 
		Set_BitMask(ControlReg, 0x10); //ControlReg 0x0C ????reader??
		Set_BitMask(TxAutoReg, 0x40); //TxASKReg 0x15 ????100%ASK??��
		Write_Reg(TxModeReg, 0x00);  //TxModeReg 0x12 ????TX CRC??��??TX FRAMING =TYPE A
		Write_Reg(RxModeReg, 0x00); //RxModeReg 0x13 ????RX CRC??��??RX FRAMING =TYPE A

//	  Write_Reg(GsNOnReg,0xF1);
//		Write_Reg(CWGsPReg,0x3F);
//		Write_Reg(ModGsPReg,0x01);

		Write_Reg(RFCfgReg,0x40);	//Bit6~Bit4 ????????
		Write_Reg(DemodReg,0x0D);
		Write_Reg(RxThresholdReg,0x84);//0x18?????	Bit7~Bit4 MinLevel Bit2~Bit0 CollLevel

		Write_Reg(AutoTestReg,0x40);//AmpRcv=1
		Write_Reg(BitFramingReg, 0x00);
	//	Write_Reg(AutoTestReg,0x00);//AmpRcv=0
		return OK;
   }
   if (type == 1)                     //ISO14443_B
   	{ 
   		Write_Reg(ControlReg, 0x10); //ControlReg 0x0C ????reader??
	    Write_Reg(TxModeReg, 0x83); //TxModeReg 0x12 ????TX CRC??��??TX FRAMING =TYPE B
		  Write_Reg(RxModeReg, 0x83); //RxModeReg 0x13 ????RX CRC??��??RX FRAMING =TYPE B
			Write_Reg(GsNOnReg, 0xF4); //GsNReg 0x27 ????ON?�� F4
//			Write_Reg(CWGsPReg, 0x40); //CWGsPReg 0x28 
			Write_Reg(ModGsPReg, 0x0E);//ModGsPReg 0x29 1B  18 14 ///10(2.5) 0E(2.8)
			Write_Reg(GsNOffReg, 0xF4); //GsNOffReg 0x23 ????OFF?��
			Write_Reg(TxAutoReg, 0x00);// TxASKReg 0x15 ????100%ASK??��
			Write_Reg(BitFramingReg, 0x00);// ??????????? BitFramingReg 0x0D  ????????????????? typer A  B?????????????????
			return OK;
	}
   if (type == 2)                     //Felica
   	{ 
   		Write_Reg(ControlReg, 0x10); //ControlReg 0x0C ????reader??
	    Write_Reg(TxModeReg, 0x92); //TxModeReg 0x12 ????TX CRC??��??212kbps,TX FRAMING =Felica
			Write_Reg(RxModeReg, 0x96); //RxModeReg 0x13 ????RX CRC??��??212kbps,Rx Multiple Enable,RX FRAMING =Felica
			Write_Reg(GsNOnReg, 0xF4); //GsNReg 0x27 ????ON?��
			Write_Reg(CWGsPReg, 0x20); //
			Write_Reg(GsNOffReg, 0x4F); //GsNOffReg 0x23 ????OFF?��
			Write_Reg(ModGsPReg, 0x20); 
			Write_Reg(TxAutoReg, 0x07);// TxASKReg 0x15 ????100%ASK??��
			return OK;
	}
	return FM175XX_ERROR;
}
/*********************************************/

/*********************************************/
/*
unsigned char FM175XX_SoftReset(void)
{	
	unsigned char regdata;
	Write_Reg(CommandReg,SoftReset);//
	Delay_100us(80);
	regdata=Read_Reg(CommandReg);
	if(regdata!=0x20)
		return FM175XX_ERROR;
	return OK;
}
*/
/*********************************************/

/*********************************************/
/*
void FM175XX_SoftPowerdown(unsigned char mode)
{
	if(mode==0)
		{
			Clear_BitMask(CommandReg,0x10);//??????????
		}
	if(mode==1)
		{
			Set_BitMask(CommandReg,0x10);//???????????
		}	
}
*/

/****************************************************************************************/
												
/****************************************************************************************/
unsigned char TypeA_Request(unsigned char *pTagType)
{
	unsigned char result,send_buff[1],rece_buff[2];
	unsigned int rece_bitlen;  
	Clear_BitMask(TxModeReg,0x80);//???TX CRC
	Clear_BitMask(RxModeReg,0x80);//???RX CRC
	Clear_BitMask(Status2Reg,0x08);//???MFCrypto1On
	Write_Reg(BitFramingReg,0x07);
	send_buff[0] = 0x26;

  	Pcd_SetTimer(1);
	result = Pcd_Comm(Transceive,send_buff,1,rece_buff,&rece_bitlen);
	
	if ((result == OK) && (rece_bitlen == 2*8))
	{   
		*pTagType     = rece_buff[0];
		*(pTagType+1) = rece_buff[1];
		return OK;
	}
  
	return FM175XX_ERROR;

	/**************************************** */
	// unsigned char rece_len;//rece_buff[2],
	// Write_Reg(TxModeReg,0x00);//Clear_BitMask(TxModeReg,0x80);//关闭TX CRC
	// Write_Reg(RxModeReg,0x00);//Clear_BitMask(RxModeReg,0x80);//关闭RX CRC
	// Write_Reg(CommandReg,0x00);//idle
	// Write_Reg(FIFOLevelReg,0x80);//clear fifo
	// Write_Reg(FIFODataReg,0x26);//send wakeup cmd
	// Write_Reg(CommandReg,0x0C);//transceive
	// Write_Reg(BitFramingReg,0x87);//start send
	// vTaskDelay(pdMS_TO_TICKS(20));
	// rece_len=Read_Reg(FIFOLevelReg);
	// if(rece_len==2)
	// {
	//  Read_FIFO(2,pTagType);
	//  return OK;
	// }
	// return FM175XX_ERROR;
	/************************************** */
}

/****************************************************************************************/

/****************************************************************************************/
unsigned char PCD_READ_CARD(unsigned char *rece_data,unsigned char *rece_len)
{
	unsigned char rets;

	if(TypeA_Request(PICC_ATQA)==OK)
	{
	 //printf("TypeA_Request OK");

	 rets=MIFARE_005M_READ_APP(rece_data,rece_len);
	 if(rets==OK)
	 {
		//printf("MIFARE_005M_READ_APP: %02X %02X %02X %02X",rece_data[0], rece_data[1], rece_data[2], rece_data[3]);
		return OK;	
	 }
	}
	vTaskDelay(pdMS_TO_TICKS(5));
	return FM175XX_ERROR;
}



/****************************************************************************************/

/****************************************************************************************/
unsigned char PCD_WRITE_CARD(unsigned char *write_data,unsigned char write_len,unsigned char *rece_data,unsigned char *rece_len)
{
	if(TypeA_Request(PICC_ATQA)==OK)
	{
	 if(MIFARE_005M_Write_APP(write_data,write_len,rece_data,rece_len)==OK)
		{
      return OK;	
		}
	}	 
	//Delay_100us(50);
	vTaskDelay(pdMS_TO_TICKS(5));
	return FM175XX_ERROR;
}

/****************************************************************************************/

/****************************************************************************************/
void hlod_card(void)
{
	unsigned char rest,conut=3;
	do{
		rest=TypeA_Request(PICC_ATQA);
		if(rest==OK)
		{
		 //LED_ON();
		 conut=3;
		}else{
		 conut--;//????????????3??????????while???????????????????
		}	
	}			
	while(conut!=0);//????????????3??????????while???????????????????
	//LED_OFF();	
	return;
}

