//#include "includeall.h"
#include "Mifare_005M.h"
#include "FM175XX.h"

#define TAG "SpiTask"

unsigned char key_mian[4]={0xC5,0x5C,0x93,0x39};//��ʽ�������õ�����	
unsigned char MIFARE_KEY[6]={0xFF,0xFF,0xFF,0xFF,0x00,0x00};//��Ƭ����Ĭ������
/*****************************************************************************************/
													
/*****************************************************************************************/
unsigned char Ultra_Read(unsigned char page,unsigned char *buff)
{
	unsigned char ret;
	unsigned char send_byte[2];
	unsigned int rece_bitlen;
	Write_Reg(TxModeReg,0x80);
	Write_Reg(RxModeReg,0x80);
    Write_Reg(BitFramingReg,0x00);
	send_byte[0]=0x30;
	send_byte[1]=page;
	Pcd_SetTimer(1);
	ret=Pcd_Comm(Transceive,send_byte,2,buff,&rece_bitlen);
	return ret;
}
/*****************************************************************************************/
													 
/*****************************************************************************************/
unsigned char Ultra_Write(unsigned char page,unsigned char *buff)
{
	unsigned char ret;
	unsigned char send_byte[6],rece_byte[1];
	unsigned int rece_bitlen=0;
	Write_Reg(TxModeReg,0x80);
	Write_Reg(RxModeReg,0x80);
    Write_Reg(BitFramingReg,0x00);
	send_byte[0]=0xA0;
	send_byte[1]=page;
	Pcd_SetTimer(2);
	ret=Pcd_Comm(Transceive,send_byte,2,rece_byte,&rece_bitlen);

	if ((rece_bitlen!=4)|((rece_byte[0]&0x0F)!=0x0A))	//���δ���յ�?4bit 1010����ʾ��ACK
	   return FM175XX_ERROR;	
	
	send_byte[0]=buff[0];
	send_byte[1]=buff[1];
	send_byte[2]=buff[2];
	send_byte[3]=buff[3];
	Pcd_SetTimer(10);
	
	ret=Pcd_Comm(Transceive,send_byte,4,rece_byte,&rece_bitlen);
	if ((rece_bitlen!=4 )|((rece_byte[0]&0x0F)!=0x0A)) //���δ���յ�?4bit 1010����ʾ��ACK
		return FM175XX_ERROR;	
	return OK;
}

/*****************************************************************************************/
																 
/*****************************************************************************************/
unsigned char Mifare_Auth(unsigned char mode,unsigned char sector,unsigned char *mifare_key,unsigned char *card_uid)
{
	unsigned char send_buff[12],rece_buff[1],result;
	unsigned int rece_bitlen=0;

	Clear_BitMask(Status2Reg,0x08);//���MFCrypto1On

	if(mode == 0x0A)//	if(mode == KEY_A)
		send_buff[0]=0x60;//60 kayA��ָ֤��
	if(mode == 0x0B)//	if(mode == KEY_B)
		send_buff[0]=0x61;//61 keyB��ָ֤��
  	send_buff[1]=(sector)*4;//��֤�����Ŀ�0��ַ
	send_buff[2]=mifare_key[0];
	send_buff[3]=mifare_key[1];
	send_buff[4]=mifare_key[2];
	send_buff[5]=mifare_key[3];
	send_buff[6]=mifare_key[4];
	send_buff[7]=mifare_key[5];
	send_buff[8]=card_uid[0];
	send_buff[9]=card_uid[1];
	send_buff[10]=card_uid[2];
	send_buff[11]=card_uid[3];

	Pcd_SetTimer(2);
	result =Pcd_Comm(MFAuthent,send_buff,12,rece_buff,&rece_bitlen);//Authent��֤

	if(result==OK)
		{
		if(Read_Reg(Status2Reg)&0x08)//�жϼ��ܱ�־λ��ȷ����֤���?
			return OK;
		else
			return FM175XX_ERROR;
		}
	return FM175XX_ERROR;
}


unsigned char MIFARE_005M_READ_APP(unsigned char *rece_data,unsigned char *rece_len)
{	
	unsigned char ble_mac1[4]={0x00};//ble_mac2[4]={0x00};
	
	//step 1 ��ȡ��ƬUID��UID�洢�ڿ�Ƭ��Block1�У�
	if(Ultra_Read(0x01,PICC_UID)==FM175XX_ERROR)
	{
		rece_data[0]=0x67;  
		rece_data[1]=0xDD;		
		*rece_len=2;		
		return FM175XX_ERROR;
	}

//��ʽ�������õ����룬ע��ͬʱ����key_mian

	MIFARE_KEY[0]=PICC_UID[1]^key_mian[1];
	MIFARE_KEY[1]=PICC_UID[3]^key_mian[3];
	MIFARE_KEY[2]=PICC_UID[0]^key_mian[0];
	MIFARE_KEY[3]=PICC_UID[2]^key_mian[2];
	MIFARE_KEY[4]=0x00;
	MIFARE_KEY[5]=0x00;
	
	
	if(Mifare_Auth(0x0A,0x00,MIFARE_KEY,PICC_UID)==FM175XX_ERROR)//	Ĭ�ϵ�ַΪ��0��KEY A ��֤
	{		
		rece_data[0]=0x67;  
		rece_data[1]=0xAA;		
		*rece_len=2;				
		return FM175XX_ERROR;	
	}

	if(Ultra_Read(0x0A,ble_mac1)==FM175XX_ERROR)//
	{			
		rece_data[0]=0x68;  
		rece_data[1]=0x00;		
		*rece_len=2;			
		return FM175XX_ERROR;	
	}	

	rece_data[0]=ble_mac1[0];		
	rece_data[1]=ble_mac1[1];		
	rece_data[2]=ble_mac1[2];		
	rece_data[3]=ble_mac1[3];	
	rece_data[4]=0X90;
	rece_data[5]=0X00;	
	*rece_len=6;								
	return OK;
}



//unsigned char MIFARE_005M_Write_APP(unsigned char *MIFARE_KEY,unsigned char *write_data,unsigned char write_len,unsigned char *rece_data,unsigned char *rece_len)
unsigned char MIFARE_005M_Write_APP(unsigned char *write_data,unsigned char write_len,unsigned char *rece_data,unsigned char *rece_len)
{
	unsigned char ble_mac1[4]={0x00};//ble_mac2[4]={0x00};
	
	//step 1 ��ȡ��ƬUID��UID�洢�ڿ�Ƭ��Block1�У�
	if(Ultra_Read(0x01,PICC_UID)==FM175XX_ERROR)
	{
		rece_data[0]=0x67;  
		rece_data[1]=0xDD;		
		*rece_len=2;			
		return FM175XX_ERROR;
	}
	
//��ʽ�������õ����룬��ע��ͬʱ����key_mian
/*
	MIFARE_KEY[0]=PICC_UID[1]^key_mian[1];
	MIFARE_KEY[1]=PICC_UID[3]^key_mian[3];
	MIFARE_KEY[2]=PICC_UID[0]^key_mian[0];
	MIFARE_KEY[3]=PICC_UID[2]^key_mian[2];
	MIFARE_KEY[4]=0x00;
	MIFARE_KEY[5]=0x00;	
	*/
	//step2 ��Կ��֤����֤����������ԿΪ6�ֽڣ�����ǰ4���ֽ�Ϊ��֤��Կ���洢�ڿ�Ƭ��Block8�У�����2���ֽ�����? 0x00
	if(Mifare_Auth(0x0A,0x00,MIFARE_KEY,PICC_UID)==FM175XX_ERROR)//	Ĭ�ϵ�ַΪ��0��KEY A ��֤
	{
		rece_data[0]=0x67;  
		rece_data[1]=0xAA;		
		*rece_len=2;			
		return FM175XX_ERROR;	
	}
	
	//step3 ���ݿ�д��	
	if(write_len==4)
	{
		if(Ultra_Write(0x0A,write_data)==FM175XX_ERROR)// ����?0x0Aд�����?
		{
			rece_data[0]=0x68;  
			rece_data[1]=0x00;		
			*rece_len=2;			
			return FM175XX_ERROR;	
		}		
  }
	else
	{
		rece_data[0]=0x68;  
		rece_data[1]=0x10;		
		*rece_len=2;			
		return FM175XX_ERROR;		
	}
	
	rece_data[0]=0x90;  
	rece_data[1]=0x00;		
	*rece_len=2;			
	return OK;
}
