#ifndef MIFARE_005M_H
#define MIFARE_005M_H

#define KEY_A 0x0A
#define KEY_B 0x0B

extern unsigned char Ultra_Read(unsigned char page,unsigned char *buff);
extern unsigned char Ultra_Write(unsigned char page,unsigned char *buff);
extern unsigned char Mifare_Auth(unsigned char mode,unsigned char sector,unsigned char *mifare_key,unsigned char *card_uid);
//extern unsigned char MIFARE_005M_READ_APP(unsigned char *MIFARE_KEY,unsigned char *rece_data,unsigned char *rece_len);
extern unsigned char MIFARE_005M_READ_APP(unsigned char *rece_data,unsigned char *rece_len);
extern unsigned char MIFARE_005M_Write_APP(unsigned char *write_data,unsigned char write_len,unsigned char *rece_data,unsigned char *rece_len);
#endif