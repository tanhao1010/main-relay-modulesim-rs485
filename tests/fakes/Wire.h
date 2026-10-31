#pragma once
#include "Arduino.h"
struct FakeWire {
  uint8_t registers[256]={};
  bool fail=false;
  uint8_t tx[64]={}, rx[64]={};
  unsigned txLen=0, rxLen=0, rxPos=0, pos=0;
  void setSCL(unsigned){} void setSDA(unsigned){} void begin(){} void setClock(unsigned){}
  void beginTransmission(uint8_t){txLen=0;}
  void write(uint8_t value){if(txLen<sizeof(tx))tx[txLen++]=value;}
  int endTransmission(){
    if(fail || txLen==0)return 4;
    pos=tx[0];for(unsigned i=1;i<txLen;++i)registers[pos++]=tx[i];
    if(txLen==1)pos=tx[0];return 0;
  }
  uint8_t requestFrom(uint8_t,uint8_t n){
    rxLen=0;rxPos=0;if(fail)return 0;
    for(unsigned i=0;i<n && rxLen<sizeof(rx);++i)rx[rxLen++]=registers[pos++];return n;
  }
  int available(){return (int)(rxLen - rxPos);}
  int read(){return (rxPos < rxLen) ? rx[rxPos++] : -1;}
};
extern FakeWire Wire;
