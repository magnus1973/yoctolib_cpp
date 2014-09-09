/*********************************************************************
 *
 * $Id: yprog.c 17497 2014-09-03 16:54:54Z seb $
 *
 * Implementation of firmware upgrade functions
 *
 * - - - - - - - - - License information: - - - - - - - - -
 *
 *  Copyright (C) 2011 and beyond by Yoctopuce Sarl, Switzerland.
 *
 *  Yoctopuce Sarl (hereafter Licensor) grants to you a perpetual
 *  non-exclusive license to use, modify, copy and integrate this
 *  file into your software for the sole purpose of interfacing
 *  with Yoctopuce products.
 *
 *  You may reproduce and distribute copies of this file in
 *  source or object form, as long as the sole purpose of this
 *  code is to interface with Yoctopuce products. You must retain
 *  this notice in the distributed source file.
 *
 *  You should refer to Yoctopuce General Terms and Conditions
 *  for additional information regarding your rights and
 *  obligations.
 *
 *  THE SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT
 *  WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 *  WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO
 *  EVENT SHALL LICENSOR BE LIABLE FOR ANY INCIDENTAL, SPECIAL,
 *  INDIRECT OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA,
 *  COST OF PROCUREMENT OF SUBSTITUTE GOODS, TECHNOLOGY OR
 *  SERVICES, ANY CLAIMS BY THIRD PARTIES (INCLUDING BUT NOT
 *  LIMITED TO ANY DEFENSE THEREOF), ANY CLAIMS FOR INDEMNITY OR
 *  CONTRIBUTION, OR OTHER SIMILAR COSTS, WHETHER ASSERTED ON THE
 *  BASIS OF CONTRACT, TORT (INCLUDING NEGLIGENCE), BREACH OF
 *  WARRANTY, OR OTHERWISE.
 *
 *********************************************************************/

#define __FILE_ID__ "yprog"
#include "ydef.h"
#ifdef YAPI_IN_YDEVICE
#include "Yocto/yocto.h"
#endif
#include "yprog.h"
#ifdef MICROCHIP_API
#include <Yocto/yapi_ext.h>
#else
#include "yproto.h"
#ifndef WINDOWS_API
#include <dirent.h>
#include <sys/stat.h>
#endif
#endif
#include "yhash.h"
#include "yjson.h"
//#define DEBUG_FIRMWARE


#ifdef MICROCHIP_API
static
#endif
const char* prog_GetCPUName(BootloaderSt *dev)
{
	const char * res="";
	switch(dev->devid_family){
	case FAMILY_PIC24FJ256DA210:
        switch(dev->devid_model){
#ifndef MICROCHIP_API
            case PIC24FJ128DA206 :
                return "PIC24FJ128DA206";
            case PIC24FJ128DA106 :
                return "PIC24FJ128DA106";
            case PIC24FJ128DA210 :
                return "PIC24FJ128DA210";
            case PIC24FJ128DA110 :
                return "PIC24FJ128DA110";
            case PIC24FJ256DA206 :
                return "PIC24FJ256DA206";
            case PIC24FJ256DA106 :
                return "PIC24FJ256DA106";
            case PIC24FJ256DA210 :
                return "PIC24FJ256DA210";
            case PIC24FJ256DA110 :
                return "PIC24FJ256DA110";
			default:
			   res = "Unknown CPU model(family PIC24FJ256DA210)";
			   break;
#else
            case PIC24FJ256DA206 :
                return "PIC24FJ256DA206";
            default: ;
#endif
		}
        break;
    case FAMILY_PIC24FJ64GB004:
        switch(dev->devid_model){
#ifndef MICROCHIP_API
            case PIC24FJ32GB002 :
                return "PIC24FJ32GB002";
            case PIC24FJ64GB002 :
                return "PIC24FJ64GB002";
            case PIC24FJ32GB004 :
                return "PIC24FJ32GB004";
            case PIC24FJ64GB004 :
                return "PIC24FJ64GB004";
            default:
				res= "Unknown CPU model(family PIC24FJ64GB004)";
				break;
#else
            case PIC24FJ64GB002 :
                return "PIC24FJ64GB002";
			default:
				break;
#endif
        }
        break;
    }
	return res;
}


//used by yprogrammer
static int  checkHardwareCompat(BootloaderSt *dev,const char *pictype)
{
    const char *cpuname=prog_GetCPUName(dev);
    if(YSTRICMP(cpuname,pictype)!=0){
        return 0;
    }
    return 1;
}



#ifdef MICROCHIP_API

int IsValidBynHead(const byn_head_multi *head, u32 size, char *errmsg)
{
    if(head->h.sign != BYN_SIGN){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "Not a firmware file");
    }
    if(YSTRLEN(head->h.serial) >= YOCTO_SERIAL_LEN){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "Bad serial");
    }
    if(YSTRLEN(head->h.product) >= YOCTO_PRODUCTNAME_LEN){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "Bad product name");
    }
    if(YSTRLEN(head->h.firmware) >= YOCTO_FIRMWARE_LEN){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "Bad firmware revision");
    }
    switch(head->h.rev) {
        case BYN_REV_V4:
            if( head->v4.nbzones > MAX_ROM_ZONES_PER_FILES){
                return YERRMSG(YAPI_INVALID_ARGUMENT,"Too many zones");
            }
            if(head->v4.datasize != size -(sizeof(byn_head_sign)+sizeof(byn_head_v4))){
                return YERRMSG(YAPI_INVALID_ARGUMENT, "Incorrect file size");
            }
            return YAPI_SUCCESS;
        case BYN_REV_V5:
#ifndef YBUILD_PATCH_WITH_BUILD
            if(head->v5.prog_version[0]){
                int byn = atoi(head->v5.prog_version);
                int tools=atoi(YOCTO_API_BUILD_NO);
                if(byn>tools){
                    return YERRMSG(YAPI_VERSION_MISMATCH, "Please upgrade the hub device first");
                }
            }
#endif
            if( head->v5.nbzones > MAX_ROM_ZONES_PER_FILES){
                return YERRMSG(YAPI_INVALID_ARGUMENT,"Too many zones");
            }
            if(head->v5.datasize != size -(sizeof(byn_head_sign)+sizeof(byn_head_v5))){
                return YERRMSG(YAPI_INVALID_ARGUMENT, "Incorrect file size");
            }
            return YAPI_SUCCESS;
        case BYN_REV_V6:
#ifndef YBUILD_PATCH_WITH_BUILD
            if(head->v6.prog_version[0]){
                int byn = atoi(head->v6.prog_version);
                int tools=atoi(YOCTO_API_BUILD_NO);
                if(byn>tools){
                    return YERRMSG(YAPI_VERSION_MISMATCH, "Please upgrade the hub device first");
                }
            }
#endif
            if( head->v6.ROM_nb_zone > MAX_ROM_ZONES_PER_FILES){
                return YERRMSG(YAPI_INVALID_ARGUMENT,"Too many ROM zones");
            }
            if( head->v6.FLA_nb_zone > MAX_FLASH_ZONES_PER_FILES){
                return YERRMSG(YAPI_INVALID_ARGUMENT,"Too many FLASH zones");
            }
            return YAPI_SUCCESS;
        default:
            break;
    }
    return YERRMSG(YAPI_INVALID_ARGUMENT, "Please upgrade the hub device first");
}

#else

int IsValidBynHead(const byn_head_multi *head, u32 size, char *errmsg)
{
    if(head->h.sign != BYN_SIGN){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "Not a valid .byn file");
    }
    if(YSTRLEN(head->h.serial) >= YOCTO_SERIAL_LEN){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "Invalid serial");
    }
    if(YSTRLEN(head->h.product) >= YOCTO_PRODUCTNAME_LEN){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "Invalid product name");
    }
    if(YSTRLEN(head->h.firmware) >= YOCTO_FIRMWARE_LEN){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "Invalid firmware revision");
    }

    switch(head->h.rev) {
        case BYN_REV_V4:
            if( head->v4.nbzones > MAX_ROM_ZONES_PER_FILES){
                return YERRMSG(YAPI_INVALID_ARGUMENT,"Too many zones in .byn file");
            }
            if(head->v4.datasize != size -(sizeof(byn_head_sign)+sizeof(byn_head_v4))){
               return YERRMSG(YAPI_INVALID_ARGUMENT, "Incorrect file size or corrupt file");
            }
            return YAPI_SUCCESS;
        case BYN_REV_V5:
            if(YSTRLEN(head->v5.prog_version) >= YOCTO_SERIAL_LEN){
                return YERRMSG(YAPI_INVALID_ARGUMENT, "Invalid programming tools revision or corrupt file");
            }
#ifndef YBUILD_PATCH_WITH_BUILD
            if(head->v5.prog_version[0]){
                 int byn = atoi(head->v5.prog_version);
                 int tools=atoi(YOCTO_API_BUILD_NO);
                 if(byn>tools){
                     return YERRMSG(YAPI_VERSION_MISMATCH, "This firmware is too recent, please upgrade your VirtualHub or Yoctopuce library");
                 }
            }
#endif
            if( head->v5.nbzones > MAX_ROM_ZONES_PER_FILES){
                return YERRMSG(YAPI_INVALID_ARGUMENT,"Too many zones in .byn file");
            }
            if(head->v5.datasize != size -(sizeof(byn_head_sign)+sizeof(byn_head_v5))){
               return YERRMSG(YAPI_INVALID_ARGUMENT, "Incorrect file size or corrupt file");
            }
            return YAPI_SUCCESS;
        case BYN_REV_V6:
            if(YSTRLEN(head->v6.prog_version) >= YOCTO_SERIAL_LEN){
                return YERRMSG(YAPI_INVALID_ARGUMENT, "Invalid programming tools revision or corrupt file");
            }
#ifndef YBUILD_PATCH_WITH_BUILD
            if(head->v6.prog_version[0]){
                int byn = atoi(head->v6.prog_version);
                int tools=atoi(YOCTO_API_BUILD_NO);
                if(byn>tools){
                    return YERRMSG(YAPI_VERSION_MISMATCH, "This firmware is too recent, please upgrade your VirtualHub or Yoctopuce library");
                }
            }
#endif
            if( head->v6.ROM_nb_zone > MAX_ROM_ZONES_PER_FILES){
                return YERRMSG(YAPI_INVALID_ARGUMENT,"Too many ROM zones in .byn file");
            }
            if( head->v6.FLA_nb_zone > MAX_FLASH_ZONES_PER_FILES){
                return YERRMSG(YAPI_INVALID_ARGUMENT,"Too many FLASH zones in .byn file");
            }
            return YAPI_SUCCESS;
        default:
            break;
    }
    return YERRMSG(YAPI_INVALID_ARGUMENT, "Unsupported file format, please upgrade your VirtualHub or Yoctopuce library");
}
#endif

int ValidateBynCompat(const byn_head_multi *head, u32 size, const char *serial, BootloaderSt *dev, char *errmsg)
{
    YPROPERR(IsValidBynHead(head,size, errmsg));
    if(YSTRNCMP(head->h.serial,serial,YOCTO_BASE_SERIAL_LEN)!=0){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "This BYN file is not designed for your device");
    }
    if(dev && !checkHardwareCompat(dev,head->h.pictype)){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "This BYN file is not designed for your device");
    }
    return 0;
}

#ifndef MICROCHIP_API
// user by yprogrammer
int IsValidBynFile(const byn_head_multi  *head, u32 size, char *errmsg)
{
    HASH_SUM ctx;
    u8       md5res[16];
    int      res;

    res = IsValidBynHead(head, size, errmsg);
    if(res == YAPI_SUCCESS && head->h.rev == BYN_REV_V6) {
        // compute MD5
        MD5Initialize(&ctx);
        MD5AddData(&ctx, ((u8*)head)+BYN_MD5_OFS_V6, size-BYN_MD5_OFS_V6);
        MD5Calculate(&ctx, md5res);
        if(memcmp(md5res, head->v6.md5chk, 16)) {
            return YERRMSG(YAPI_INVALID_ARGUMENT,"Invalid checksum");
        }
    }
    return res;
}
#endif

#ifdef CPU_BIG_ENDIAN


#define BSWAP_U16(NUM)  (((NUM )>> 8) | ((NUM) << 8))
#define BSWAP_U32(NUM) ((((NUM) >> 24) & 0xff) | (((NUM) << 8) & 0xff0000) | (((NUM) >> 8) & 0xff00) | (((NUM) << 24) & 0xff000000 ))

void decode_byn_head_multi(byn_head_multi *head)
{
    head->h.sign = BSWAP_U32(head->h.sign);
    head->h.rev = BSWAP_U16(head->h.rev);
    switch (head->h.rev) {
    case BYN_REV_V4:
        head->v4.nbzones = BSWAP_U32(head->v4.nbzones);
        head->v4.datasize = BSWAP_U32(head->v4.datasize);
        break;
    case BYN_REV_V5:
        head->v5.pad = BSWAP_U16(head->v5.pad);
        head->v5.nbzones = BSWAP_U32(head->v5.nbzones);
        head->v5.datasize = BSWAP_U32(head->v5.datasize);
        break;
    case BYN_REV_V6:
        head->v6.ROM_total_size = BSWAP_U32(head->v6.ROM_total_size);
        head->v6.FLA_total_size = BSWAP_U32(head->v6.FLA_total_size);
        break;
    default:
        break;
    }
}

void decode_byn_zone(byn_zone *zone)
{
    zone->addr_page = BSWAP_U32(zone->addr_page);
    zone->len = BSWAP_U32(zone->len);
}


#endif











#if !defined(MICROCHIP_API)
// Return 1 if the communication channel to the device is busy
// Return 0 if there is no ongoing transaction with the device
int ypIsSendBootloaderBusy(BootloaderSt *dev)
{
    return 0;
}


// Return 0 if there command was successfully queued for sending
// Return -1 if the output channel is busy and the command could not be sent
int ypSendBootloaderCmd(BootloaderSt *dev, const USB_Packet *pkt,char *errmsg)
{
	return yyySendPacket(&dev->iface,pkt,errmsg);
}

// Return 0 if a reply packet was available and returned
// Return -1 if there was no reply available or on error
int ypGetBootloaderReply(BootloaderSt *dev, USB_Packet *pkt,char *errmsg)
{
	pktItem *ptr;
    // clear the dest buffer to avoid any misinterpretation
    memset(pkt->prog.raw, 0, sizeof(USB_Packet));
	YPROPERR(yPktQueueWaitAndPopD2H(&dev->iface,&ptr,10,errmsg));
    if(ptr){
        yTracePtr(ptr);
        memcpy(pkt,&ptr->pkt,sizeof(USB_Packet));
        yFree(ptr);
        return 0;
    }
	return YAPI_TIMEOUT; // not a fatal error, handled by caller
}
#endif



#if !defined(MICROCHIP_API)
//pool a packet form usb for a specific device
int BlockingRead(BootloaderSt *dev,USB_Packet *pkt, int maxwait, char *errmsg)
{
	pktItem *ptr;
	YPROPERR(yPktQueueWaitAndPopD2H(&dev->iface,&ptr,maxwait,errmsg));
    if (ptr) {
	    yTracePtr(ptr);
		memcpy(pkt,&ptr->pkt,sizeof(USB_Packet));
		yFree(ptr);
        return YAPI_SUCCESS;
	}
	return YERR(YAPI_TIMEOUT);
}

int SendDataPacket( BootloaderSt *dev,int program, u32 address, u8 *data,int nbinstr,char *errmsg)
{

    USB_Packet  pkt;
    //USB_Prog_Packet *pkt = &dev->iface.txqueue->pkt.prog;
    memset(&pkt.prog,0,sizeof(USB_Prog_Packet));
    if(program){
        pkt.prog.pkt.type = PROG_PROG;
    }else{
        pkt.prog.pkt.type = PROG_VERIF;
    }
    pkt.prog.pkt.adress_low = address &0xffff;
    pkt.prog.pkt.addres_high = (address>>16)&0xff;
    if(nbinstr > MAX_INSTR_IN_PACKET){
        nbinstr = MAX_INSTR_IN_PACKET;
    }
    if(nbinstr){
        memcpy(pkt.prog.pkt.data,data,nbinstr*3);
        pkt.prog.pkt.size= nbinstr;
    }

    YPROPERR(ypSendBootloaderCmd(dev,&pkt,errmsg));
    return nbinstr;
}


#if 0

//used by yprogrammer
static int prog_RebootDevice(BootloaderSt *dev,u16 signature, char *errmsg)
{
    int res;
    char    suberr[YOCTO_ERRMSG_LEN];
    USB_Packet pkt;
    memset(&pkt,0,sizeof(USB_Packet));
    pkt.prog.pkt_ext.type = PROG_REBOOT;
    pkt.prog.pkt_ext.opt.btsign = signature;
    res = ypSendBootloaderCmd(dev,&pkt,suberr);
	if (YISERR(res)){
		dbglog("Reboot cmd has generated an error:%s\n",suberr);
	}
	return YAPI_SUCCESS;

}

//used by yprogrammer
static int prog_BlankDevice(BootloaderSt *dev,char *errmsg)
{
    int     res;
    char    suberr[YOCTO_ERRMSG_LEN];
    USB_Packet  pkt;
    int         delay;

    memset(&pkt,0,sizeof(USB_Prog_Packet));
    if(dev->ext_total_pages){
        pkt.prog.pkt_ext.type = PROG_ERASE;
        SET_PROG_POS_PAGENO(pkt.prog.pkt_ext, dev->first_code_page, 0);
        pkt.prog.pkt_ext.opt.npages = dev->ext_total_pages - dev->first_code_page;
        res= ypSendBootloaderCmd(dev,&pkt,suberr);
        delay = 1000 + ((pkt.prog.pkt_ext.opt.npages / 16) *30); //wait 30ms per 16 pages
    }else{
        pkt.prog.pkt.type = PROG_ERASE;
        res= ypSendBootloaderCmd(dev,&pkt,suberr);
        delay = 1000 + (dev->last_addr>>6);
    }
    if (YISERR(res)) {
        return FusionErrmsg(res,errmsg,"Unable to blank the device",suberr);
    }
    res = prog_GetDeviceInfo(dev, delay, errmsg);
    if(YISERR(res)){
        return FusionErrmsg(res,errmsg,"Unable to blank the device (timeout)",suberr);
    }
    return YAPI_SUCCESS;
}

static int prog_FlashVerifBlock(BootloaderSt *dev,int flash,u32 startAddr, u8 *data,u32 size,char *errmsg)
{
    u32 nb_instr;
    u32 instr_no;
    USB_Packet respkt;


    instr_no = startAddr/2;
    if((startAddr % (dev->pr_blk_size*2)) !=0){
        return YERRMSG(YAPI_INVALID_ARGUMENT,"Prog block not on a boundary");
    }

    nb_instr = size/3;
    if(nb_instr < (u32)dev->pr_blk_size){
        return YERRMSG(YAPI_INVALID_ARGUMENT,"Prog block too small");
    }
    if(nb_instr > (dev->settings_addr-startAddr)/2){
        nb_instr = (dev->settings_addr-startAddr)/2;
    }
    while(nb_instr){
        u32 ofs          = instr_no % dev->pr_blk_size;
        u32 block_number = instr_no / dev->pr_blk_size;
        u32 block_addr   = block_number * dev->pr_blk_size * 2;

        while(nb_instr &&  ofs < dev->pr_blk_size ){
            int sent;
            int size = nb_instr < 20 ? (int)nb_instr : 20;
            sent = SendDataPacket(dev,flash,block_addr,data,size,errmsg);
            if(sent<0){
                return YERRMSG(YAPI_IO_ERROR,"Transmit failed");
            }
            ofs      += sent;
            instr_no += sent;
            nb_instr -= sent;
            data     += (sent*3);
        }
        YPROPERR( BlockingRead(dev,&respkt, 1000, errmsg) );
        if(respkt.prog.pktinfo.type != PROG_PROG){
            return YERRMSG(YAPI_INVALID_ARGUMENT,"Block verification failed");
        }
    }

    return YAPI_SUCCESS;
}

//used by yprogrammer
static int prog_FlashBlock(BootloaderSt *dev,u32 startAddr, u8 *data,int size,char *errmsg)
{
    int     res;
    char    suberr[YOCTO_ERRMSG_LEN];

    res = prog_FlashVerifBlock(dev,1,startAddr,data,size,suberr);
    if(YISERR(res)){
        int len;
        if(errmsg){
            YSTRCPY(errmsg,YOCTO_ERRMSG_LEN,"Flash failed (try to blank device before) : ");
            len=YSTRLEN(errmsg);
            YSTRNCAT(errmsg,YOCTO_ERRMSG_LEN,suberr,YOCTO_ERRMSG_LEN-len);
        }
    }
    return res;
}


static int prog_VerifyBlock(BootloaderSt *dev,u32 startAddr, u8 *data,int size,char *errmsg)
{
    int     res;
    char    suberr[YOCTO_ERRMSG_LEN];

    res = prog_FlashVerifBlock(dev,0,startAddr,data,size,suberr);
    if(YISERR(res)){
        int len;
        if(errmsg){
            YSTRCPY(errmsg,YOCTO_ERRMSG_LEN,"Verification failed: ");
            len=YSTRLEN(errmsg);
            YSTRNCAT(errmsg,YOCTO_ERRMSG_LEN,suberr,YOCTO_ERRMSG_LEN-len);
        }
    }
    return res;
}


static int prog_FlashFlash(yFlashArg *arg,int step,BootloaderSt *dev, newmemzones *zones,char *errmsg)
{
    u32 currzone;
    u32 page;
    u32 len;
    USB_Packet pkt;
    char suberr[YOCTO_ERRMSG_LEN];
    int res;

    YASSERT(dev->first_yfs3_page !=0xffff && dev->first_code_page!=0xffff);

    for(currzone=0; currzone < zones->nbrom + zones->nbflash ;currzone++) {
        u8  *data,*verif_data;
        u32 stepB;
        u32 addr, datasize;
        if(arg->callback) arg->callback(step,10,arg->context);
        if(step<8)
			(step)++;

        if(currzone < zones->nbrom) {
            page      = (u32)dev->first_code_page * dev->ext_page_size + 3*zones->rom[currzone].addr/2;
            len       = zones->rom[currzone].len;
            data      = zones->rom[currzone].ptr;
        } else {
            page      = (u32)dev->first_yfs3_page * dev->ext_page_size + zones->flash[currzone-zones->nbrom].page;
            len       = zones->flash[currzone-zones->nbrom].len;
            data      = zones->flash[currzone-zones->nbrom].ptr;
        }
        verif_data = data;
#ifdef DEBUG_FIRMWARE
        dbglog("Flash zone %d : %x(%x)\n",currzone,page,len);
#endif
        if((page & 1) != 0 || (len & 1) != 0) {
            dbglog("Prog block not on a word boundary (%d+%d)\n", page, len);
            YSTRCPY(errmsg,YOCTO_ERRMSG_LEN,"Prog block not on a word boundary");
            return -1;
        }

        while (len>0) {
            if(currzone < zones->nbrom && page >= (u32)dev->first_yfs3_page * dev->ext_page_size) {
                // skip end of ROM image past reserved flash zone
#ifdef DEBUG_FIRMWARE
                dbglog("Drop ROM data past firmware boundary (zone %d)\n", currzone);
#endif
                break;
            }
            stepB =0;
            do{
                addr = page + stepB;
                memset(&pkt,0,sizeof(USB_Packet));
                SET_PROG_POS_PAGENO(pkt.prog.pkt_ext, addr / dev->ext_page_size,  addr >> 2);
                datasize = dev->ext_page_size - (addr & (dev->ext_page_size-1));
                if(datasize > MAX_BYTE_IN_PACKET) {
                    datasize = MAX_BYTE_IN_PACKET;
                }
                if(stepB + datasize > len) {
                    datasize = len - stepB;
                }
                YASSERT((datasize & 1) == 0);
                pkt.prog.pkt_ext.size = (u8)(datasize / 2);
                pkt.prog.pkt_ext.type = PROG_PROG;
#ifdef DEBUG_FIRMWARE
                dbglog("Flash at %x:%x (%x bytes) found at %x (%x more in zone)\n",pkt.prog.pkt_ext.pageno,
                  ((u32)pkt.prog.pkt_ext.dwordpos_hi*1024)+(u32)pkt.prog.pkt_ext.dwordpos_lo*4,
                  2*pkt.prog.pkt_ext.size, data, len);
#endif
                memcpy( pkt.prog.pkt_ext.opt.data,data, datasize);
                YPROPERR(ypSendBootloaderCmd(dev,&pkt,errmsg));
                data  += datasize;
                stepB += datasize;
			} while ( ((u16)( (addr & (dev->ext_page_size-1)) + datasize) < dev->ext_page_size) && (stepB <len));

            // pageno is already set properly
            addr = page;
            SET_PROG_POS_PAGENO(pkt.prog.pkt_ext, addr / dev->ext_page_size, addr >> 2);
            pkt.prog.pkt.type = PROG_VERIF;
            if((res=ypSendBootloaderCmd(dev,&pkt,suberr))<0){
                dbglog("Unable to send verif pkt\n");
                return FusionErrmsg(res,errmsg,"Unable to blank the device",suberr);
            }
            do{
                u32 pageno, pos;
                // clear the buffer
                pkt.prog.pkt.type = PROG_NOP;
                res = BlockingRead(dev,&pkt, 1000, suberr);
                if(YISERR(res)){ return FusionErrmsg(res,errmsg,"Unable to get verification packet",suberr);}

                if(pkt.prog.pkt.type != PROG_VERIF) {
                    dbglog("Invalid verif pkt\n");
                    return YERRMSG(YAPI_IO_ERROR,"Block verification failed");
                }
                GET_PROG_POS_PAGENO(pkt.prog.pkt_ext, pageno, pos);
                pos <<= 2;
#ifdef DEBUG_FIRMWARE
                dbglog("Verif at %x:%x (up to %x bytes)\n",pageno,pos , 2*pkt.prog.pkt_ext.size);
#endif

                addr = pageno * dev->ext_page_size + pos;
                YASSERT(addr >= page);
                if(addr < page + stepB) {
                    // packet is in verification range
                    datasize = 2 * pkt.prog.pkt_ext.size;
                    if(addr + datasize >= page + stepB) {
                        datasize = page + stepB - addr;
                    }
                    if(memcmp(verif_data+(addr-page), pkt.prog.pkt_ext.opt.data, datasize) != 0) {
                        dbglog("Flash verification failed\n");
                        return YERRMSG(YAPI_IO_ERROR,"Flash verification failed");
                    }
                } else {
#ifdef DEBUG_FIRMWARE
                    dbglog("Skip verification for block at %x (block ends at %x)\n", addr, page + stepB);
#endif
                }
            }while((addr & (dev->ext_page_size-1)) + 2 * (u32)pkt.prog.pkt_ext.size < (u32)dev->ext_page_size);
            verif_data += stepB;
            page += stepB;
            len -= stepB;
            if(len > 0 && currzone < zones->nbrom && page >= (u32)dev->first_yfs3_page * dev->ext_page_size) {
                // skip end of ROM image past reserved flash zone
#ifdef DEBUG_FIRMWARE
                dbglog("Drop ROM data past firmware boundary (zone %d at offset %x)\n",currzone, data);
#endif
                data += len;
                len = 0;
            }
        }
    }
    return 0;
}


static int  prog_FlashDevice(yFlashArg *arg, int realyflash, char *errmsg)
{
    BootloaderSt dev;
    char suberrmsg[YOCTO_ERRMSG_LEN];
    int res;
	u32 z;
    newmemzones zones;

#ifdef DEBUG_FIRMWARE
    dbglog("Upgrade firmware(%s,%d)\n",arg->serial,realyflash);
#endif
    if(arg->callback) arg->callback(1,10,arg->context);
    if(arg->OSDeviceName!= NULL){
        res = yUSBGetBooloader(NULL,arg->OSDeviceName,&dev.iface,suberrmsg);
        if(YISERR(res)){
            return FusionErrmsg(res,errmsg,"Unable to open the device by name",suberrmsg);
        }
    }else{
        res=yUSBGetBooloader(arg->serial,NULL,&dev.iface,suberrmsg);
        if(YISERR(res)){
            return FusionErrmsg(res,errmsg,"Unable to open the device by serial",suberrmsg);
        }
    }
    YPROPERR(yyySetup(&dev.iface,errmsg));
#ifdef DEBUG_FIRMWARE
    dbglog("get infos\n");
#endif

    res=prog_GetDeviceInfo(&dev, 1000, errmsg);
    if(YISERR(res)){
        yyyPacketShutdown(&dev.iface);
        return res;
    }
#ifdef DEBUG_FIRMWARE
    dbglog("decode byn\n");
#endif
    if(arg->callback) arg->callback(2,10,arg->context);
    res = DecodeBynFile(arg->firmwarePtr,arg->firmwareLen,&zones,dev.iface.serial,&dev,errmsg);
    if(res<0)
        return res;
#ifdef DEBUG_FIRMWARE
    dbglog("blank dev\n");
#endif
    if(arg->callback) arg->callback(3,10,arg->context);
    if(realyflash){
        res = prog_BlankDevice(&dev,errmsg);
        if(YISERR(res)){
            FreeZones(&zones);
            yyyPacketShutdown(&dev.iface);
            return res;
        }
    }

    if(dev.ext_total_pages ==0){
        int step = 4;
        for (z=0 ; z < zones.nbrom  ; z++){
#ifdef DEBUG_FIRMWARE
			dbglog("prog zone %d\n",z);
#endif

            if(arg->callback) arg->callback(step,10,arg->context);
            if(step<8)
                step++;
            if(realyflash){
                res = prog_FlashBlock(&dev, zones.rom[z].addr,zones.rom[z].ptr,zones.rom[z].len,errmsg);
            }else{
                res = prog_VerifyBlock(&dev, zones.rom[z].addr,zones.rom[z].ptr,zones.rom[z].len,errmsg);
            }
            if(YISERR(res)){
                FreeZones(&zones);
                yyyPacketShutdown(&dev.iface);
                return res;
            }
        }
        if(arg->callback) arg->callback(9,10,arg->context);
        if(realyflash){
            res = prog_RebootDevice(&dev,START_APPLICATION_SIGN,errmsg);
        }else{
            res = YAPI_SUCCESS;
        }
    }else{
        res = prog_FlashFlash(arg,4,&dev, &zones, errmsg);
        if(YISERR(res)){
            FreeZones(&zones);
            yyyPacketShutdown(&dev.iface);
            return res;
        }
        if(arg->callback) arg->callback(9,10,arg->context);
        res = prog_RebootDevice(&dev,START_AUTOFLASHER_SIGN,errmsg);
        if(YISERR(res)){
            FreeZones(&zones);
            yyyPacketShutdown(&dev.iface);
            return res;
        }
    }

    FreeZones(&zones);
    yyyPacketShutdown(&dev.iface);
    if(YISERR(res)){
        return res;
    }

    if(arg->callback) arg->callback(10,10,arg->context);

    return YAPI_SUCCESS;
}



//used by yprogrammer
// retrun 0 for legacy bootloader and 1 for flash bootloader
static int prog_GetDeviceInfo(BootloaderSt *dev, int maxwait, char *errmsg)
{
    int     res;
    char    suberr[YOCTO_ERRMSG_LEN];
    const char    *nicemsg="Unable to get device infos";
    USB_Packet pkt;
    memset(&pkt,0,sizeof(USB_Prog_Packet));
    pkt.prog.pkt.type = PROG_INFO;
    res = ypSendBootloaderCmd(dev,&pkt,suberr);
    if(YISERR(res)){ return FusionErrmsg(res,errmsg,nicemsg,suberr);}
    pkt.prog.pkt.type = PROG_NOP;
    res = BlockingRead(dev,&pkt, maxwait, suberr);
    if(YISERR(res)){ return FusionErrmsg(res,errmsg,nicemsg,suberr);}

    switch(pkt.prog.pktinfo.type){
        case PROG_INFO:
            dev->er_blk_size   = DECODE_U16(pkt.prog.pktinfo.er_blk_size);
            dev->pr_blk_size   = DECODE_U16(pkt.prog.pktinfo.pr_blk_size);
            dev->last_addr     = DECODE_U32(pkt.prog.pktinfo.last_addr);
            dev->settings_addr = DECODE_U32(pkt.prog.pktinfo.settings_addr);
            dev->devid_family  = DECODE_U16(pkt.prog.pktinfo.devidl)>>8;
            dev->devid_model   = DECODE_U16(pkt.prog.pktinfo.devidl) & 0xff;
            dev->devid_rev     = DECODE_U16(pkt.prog.pktinfo.devidh);
            dev->startconfig   = DECODE_U32(pkt.prog.pktinfo.config_start);
            dev->endofconfig   = DECODE_U32(pkt.prog.pktinfo.config_stop);
            dev->ext_jedec_id  = 0xffff;
            dev->ext_page_size = 0xffff;
            dev->ext_total_pages = 0;
            dev->first_code_page = 0xffff;
            dev->first_yfs3_page = 0xffff;
			break;
        case PROG_INFO_EXT:
            dev->er_blk_size   = DECODE_U16(pkt.prog.pktinfo_ext.er_blk_size);
            dev->pr_blk_size   = DECODE_U16(pkt.prog.pktinfo_ext.pr_blk_size);
            dev->last_addr     = DECODE_U32(pkt.prog.pktinfo_ext.last_addr);
            dev->settings_addr = DECODE_U32(pkt.prog.pktinfo_ext.settings_addr);
            dev->devid_family  = DECODE_U16(pkt.prog.pktinfo_ext.devidl) >> 8;
            dev->devid_model   = DECODE_U16(pkt.prog.pktinfo_ext.devidl) & 0xff;
            dev->devid_rev     = DECODE_U16(pkt.prog.pktinfo_ext.devidh);
            dev->startconfig   = DECODE_U32(pkt.prog.pktinfo_ext.config_start);
            dev->endofconfig   = DECODE_U32(pkt.prog.pktinfo_ext.config_stop);
            dev->ext_jedec_id    = DECODE_U16(pkt.prog.pktinfo_ext.ext_jedec_id);
            dev->ext_page_size   = DECODE_U16(pkt.prog.pktinfo_ext.ext_page_size);
            dev->ext_total_pages = DECODE_U16(pkt.prog.pktinfo_ext.ext_total_pages);
            dev->first_code_page = DECODE_U16(pkt.prog.pktinfo_ext.first_code_page);
            dev->first_yfs3_page = DECODE_U16(pkt.prog.pktinfo_ext.first_yfs3_page);
            return 1;
        default:
            return FusionErrmsg(YAPI_IO_ERROR,errmsg,nicemsg,"Invalid Prog packet");
    }

	return YAPI_SUCCESS;
}



int DecodeBynFile(const u8 *buffer,u32 size,newmemzones *zones,const char *serial, BootloaderSt *dev,char *errmsg)
{
    const byn_head_multi  *head=(byn_head_multi*)buffer;
    const u8 *rom=NULL;
    const u8 *fla=NULL;
    u32  i;
    int  res=ValidateBynCompat(head,size,serial,dev,errmsg);
    YPROPERR(res);
    memset(zones, 0,sizeof(newmemzones));
    switch(head->h.rev) {
        case BYN_REV_V4:
            rom = buffer+BYN_HEAD_SIZE_V4;
            zones->nbrom =  head->v4.nbzones;
            break;
        case BYN_REV_V5:
            rom = buffer+BYN_HEAD_SIZE_V5;
            zones->nbrom =  head->v5.nbzones;
            break;
        case BYN_REV_V6:
            rom = buffer+BYN_HEAD_SIZE_V6;
            zones->nbrom =  head->v6.ROM_nb_zone;
            fla = rom+head->v6.ROM_total_size;
            zones->nbflash =  head->v6.FLA_nb_zone;
            break;
        default:
            return YERRMSG(YAPI_INVALID_ARGUMENT, "Unsupported file format (upgrade your tools)");
    }

    //decode all rom zones
    for(i=0 ; i<zones->nbrom && i < MAX_ROM_ZONES_PER_FILES ; i++){
        byn_zone *zone=(byn_zone*)rom;
        zones->rom[i].addr = zone->addr_page;
        zones->rom[i].len  = zone->len;
        zones->rom[i].ptr = (u8*)yMalloc(zone->len);
        memcpy(zones->rom[i].ptr,rom+sizeof(byn_zone),zone->len);
        zones->rom[i].nbinstr = zone->len/3;
        zones->rom[i].nbblock = zones->rom[i].nbinstr/PROGRAM_BLOCK_SIZE_INSTR;
        rom += sizeof(byn_zone)+zone->len;
    }
    //decode all Flash zones
    for(i=0 ; i<zones->nbflash && i < MAX_FLASH_ZONES_PER_FILES ; i++){
        byn_zone *zone=(byn_zone*)fla;
        zones->flash[i].page = zone->addr_page;
        zones->flash[i].len  = zone->len;
        zones->flash[i].ptr = (u8*)yMalloc(zone->len);
        memcpy(zones->flash[i].ptr,rom+sizeof(byn_zone),zone->len);
        fla += sizeof(byn_zone)+zone->len;
    }

    return 0;
}

void FreeZones(newmemzones *zones)
{
    u32 i;
    for(i=0 ; i<zones->nbrom && i<MAX_ROM_ZONES_PER_FILES ; i++){
        if(zones->rom[i].ptr){
            yFree(zones->rom[i].ptr);
        }
    }
    for(i=0 ; i < zones->nbflash && i <MAX_FLASH_ZONES_PER_FILES  ; i++){
        if(zones->flash[i].ptr){
            yFree(zones->flash[i].ptr);
        }
    }
    memset(zones,0,sizeof(newmemzones));
}

#endif


int yUSBGetBooloader(const char *serial, const char * name,  yInterfaceSt *iface,char *errmsg)
{

    int             nbifaces=0;
    yInterfaceSt    *curif;
    yInterfaceSt    *runifaces=NULL;
    int             i;

    YPROPERR(yyyUSBGetInterfaces(&runifaces,&nbifaces,errmsg));
    //inspect all interfaces
    for(i=0, curif = runifaces ; i < nbifaces ; i++, curif++){
        // skip real devices
        if(curif->deviceid >YOCTO_DEVID_BOOTLOADER)
            continue;
#ifdef WINDOWS_API
        if(name !=NULL && YSTRICMP(curif->devicePath,name)==0){
            if (iface)
                memcpy(iface,curif,sizeof(yInterfaceSt));
            yFree(runifaces);
            return YAPI_SUCCESS;
        }else
#endif
        if(serial!=NULL && YSTRCMP(curif->serial,serial)==0){
            if (iface)
                memcpy(iface,curif,sizeof(yInterfaceSt));
            yFree(runifaces);
            return YAPI_SUCCESS;
        }
    }
    // free all tmp ifaces
    if(runifaces){
        yFree(runifaces);
    }
    return YERR(YAPI_DEVICE_NOT_FOUND);
}

#endif

#ifndef YAPI_IN_YDEVICE
static int yLoadFirmwareFile(const char * filename, u8 **buffer, char *errmsg)
{
    FILE *f = NULL;
    int  size;
    int  readed;

    if (YFOPEN(&f, filename, "rb") != 0) {
        return YERRMSG(YAPI_IO_ERROR, "unable to access file");
    }
    fseek(f, 0, SEEK_END);
    size = (int)ftell(f);
    if (size > 0x100000){
        fclose(f);
        return YERR(YAPI_IO_ERROR);
    }
    *buffer = yMalloc(size);
    fseek(f, 0, SEEK_SET);
    readed = (int)fread(*buffer, 1, size, f);
    fclose(f);
    if (readed != size) {
        return YERRMSG(YAPI_IO_ERROR, "short read");
    }
    return size;
}


static void yGetFirmware(u32 ofs, u8 *dst, u16 size)
{
    YASSERT(fctx.firmware);
    YASSERT(ofs + size <= fctx.len);
    memcpy(dst, fctx.firmware + ofs, size);
}


#endif



#ifdef YAPI_IN_YDEVICE

#define ulog ylog
#define ulogU16 ylogU16
#define ulogChar ylogChar

#define uSetGlobalProgress(prog) {}

#else

#define ytime() ((u32) yapiGetTickCount())
#define Flash_ready()  1



#define ulog(str) dbglog("%s",str)
#define ulogU16(val) dbglog("%x",val)
#define ulogChar(val) dbglog("%c",val)


static void uSetGlobalProgress(int prog)
{
    yEnterCriticalSection(&fctx.cs); //fixme a bit of an overkill
    yContext->fuCtx.global_progress = prog;
    yLeaveCriticalSection(&fctx.cs);
}

#endif

static void uLogProgress( const char *msg)
{
    yEnterCriticalSection(&fctx.cs);
    YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN, msg);
#ifdef YAPI_IN_YDEVICE
    hProgLog(msg);
#endif
#ifdef DEBUG_FIRMWARE
    dbglog("FIRM:%d:%s\n", fctx.progress, fctx.errmsg);
#endif
    yLeaveCriticalSection(&fctx.cs);
}

#ifdef MICROCHIP_API
#define uGetBootloader(serial,ifaceptr)   yGetBootloaderPort(serial,ifaceptr)
#else
#define uGetBootloader(serial,ifaceptr)   yUSBGetBooloader(serial, NULL, ifaceptr,NULL)
#endif


FIRMWARE_CONTEXT fctx;





// these two variable have been extracted from FIRMWARE_CONTEXT
// to prevent some compiler to missalign them (GCC on raspberry PI)

// WATCH OUT: in the YoctoHub, these buffers are the unique storage
//            for all programming packets going in and out, including
//            on the trunk port. They are be accessed by IRQ handler.
//            do not change packet "sent" until no more busy.
BootloaderSt            firm_dev;
USB_Packet              firm_pkt;


void uProgInit(void)
{
#ifndef MICROCHIP_API
    // BYN header must have an even number of bytes
    YASSERT((sizeof(byn_head_multi)& 1) == 0);
#endif
    memset(&fctx, 0, sizeof(fctx));
    fctx.stepA = FLASH_DONE;
    memset(&firm_dev, 0, sizeof(firm_dev));
#ifndef YAPI_IN_YDEVICE
    yContext->fuCtx.global_progress = 100;
#endif
#ifndef MICROCHIP_API
    yInitializeCriticalSection(&fctx.cs);
#endif
}

#ifndef MICROCHIP_API
void  uProgFree(void)
{
    int fuPending;
    do {

        yEnterCriticalSection(&fctx.cs);
        if (yContext->fuCtx.global_progress <0 || yContext->fuCtx.global_progress >= 100) {
            fuPending = 0;
        } else{
            fuPending = 1;
        }
        yLeaveCriticalSection(&fctx.cs);
        if (fuPending){
            yApproximateSleep(1);
        }
    } while (fuPending);
    if (yContext->fuCtx.serial)
        yFree(yContext->fuCtx.serial);
    if (yContext->fuCtx.firmwarePath)
        yFree(yContext->fuCtx.firmwarePath);
    yDeleteCriticalSection(&fctx.cs);
    memset(&fctx, 0, sizeof(fctx));
}
#endif



//-1 = error 0= retry (the global fctx.stepA is allready updated)
static int uGetDeviceInfo(void)
{
    switch(fctx.stepB){
    case 0:
        memset(&firm_pkt,0,sizeof(USB_Prog_Packet));
        firm_pkt.prog.pkt.type = PROG_INFO;
        if(ypSendBootloaderCmd(&firm_dev,&firm_pkt,NULL)<0){
#ifdef DEBUG_FIRMWARE
            ulog("Cannot send GetInfo pkt\n");
#endif
            YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"GetInfo err");
            return -1;
        }
        fctx.stepB++;
        fctx.timeout = ytime() + PROG_GET_INFO_TIMEOUT;
        // no break on purpose;
    case 1:
        if(ypGetBootloaderReply(&firm_dev, &firm_pkt,NULL)<0){
            if((s32)(fctx.timeout - ytime()) < 0) {
#ifdef DEBUG_FIRMWARE
                ulog("Bootloader did not respond to GetInfo pkt\n");
#endif
                YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"GetInfo err");
                return -1;
            }
            return 0;
        }
        fctx.stepB++;
        // no break on purpose;
    case 2:
        if(firm_pkt.prog.pkt.type == PROG_INFO) {
#ifdef DEBUG_FIRMWARE
            ulog("PROG_INFO received\n");
#endif
            firm_dev.er_blk_size   = DECODE_U16(firm_pkt.prog.pktinfo.er_blk_size);
            firm_dev.pr_blk_size   = DECODE_U16(firm_pkt.prog.pktinfo.pr_blk_size);
            firm_dev.last_addr     = DECODE_U32(firm_pkt.prog.pktinfo.last_addr);
            firm_dev.settings_addr = DECODE_U32(firm_pkt.prog.pktinfo.settings_addr);
            firm_dev.devid_family  = DECODE_U16(firm_pkt.prog.pktinfo.devidl)>>8;
            firm_dev.devid_model   = DECODE_U16(firm_pkt.prog.pktinfo.devidl) & 0xff;
            firm_dev.devid_rev     = DECODE_U16(firm_pkt.prog.pktinfo.devidh);
            firm_dev.startconfig   = DECODE_U32(firm_pkt.prog.pktinfo.config_start);
            firm_dev.endofconfig   = DECODE_U32(firm_pkt.prog.pktinfo.config_stop);
#ifndef MICROCHIP_API
            firm_dev.ext_jedec_id    = 0xffff;
            firm_dev.ext_page_size   = 0xffff;
            firm_dev.ext_total_pages = 0;
            firm_dev.first_code_page = 0xffff;
            firm_dev.first_yfs3_page = 0xffff;
#endif
            uLogProgress("Device info retrieved");
            fctx.stepB = 0;
            fctx.stepA = FLASH_VALIDATE_BYN;
#ifndef MICROCHIP_API
        } else if(firm_pkt.prog.pkt.type == PROG_INFO_EXT) {
#ifdef DEBUG_FIRMWARE
            ulog("PROG_INFO_EXT received\n");
#endif
            firm_dev.er_blk_size   = DECODE_U16(firm_pkt.prog.pktinfo_ext.er_blk_size);
            firm_dev.pr_blk_size   = DECODE_U16(firm_pkt.prog.pktinfo_ext.pr_blk_size);
            firm_dev.last_addr     = DECODE_U32(firm_pkt.prog.pktinfo_ext.last_addr);
            firm_dev.settings_addr = DECODE_U32(firm_pkt.prog.pktinfo_ext.settings_addr);
            firm_dev.devid_family  = DECODE_U16(firm_pkt.prog.pktinfo_ext.devidl) >> 8;
            firm_dev.devid_model   = DECODE_U16(firm_pkt.prog.pktinfo_ext.devidl) & 0xff;
            firm_dev.devid_rev     = DECODE_U16(firm_pkt.prog.pktinfo_ext.devidh);
            firm_dev.startconfig   = DECODE_U32(firm_pkt.prog.pktinfo_ext.config_start);
            firm_dev.endofconfig   = DECODE_U32(firm_pkt.prog.pktinfo_ext.config_stop);
            firm_dev.ext_jedec_id    = DECODE_U16(firm_pkt.prog.pktinfo_ext.ext_jedec_id);
            firm_dev.ext_page_size   = DECODE_U16(firm_pkt.prog.pktinfo_ext.ext_page_size);
            firm_dev.ext_total_pages = DECODE_U16(firm_pkt.prog.pktinfo_ext.ext_total_pages);
            firm_dev.first_code_page = DECODE_U16(firm_pkt.prog.pktinfo_ext.first_code_page);
            firm_dev.first_yfs3_page = DECODE_U16(firm_pkt.prog.pktinfo_ext.first_yfs3_page);
            uLogProgress("Device info retrieved");
            fctx.stepB = 0;
            fctx.stepA = FLASH_VALIDATE_BYN;
#endif
        } else {
#ifdef DEBUG_FIRMWARE
            ulog("Not a PROG_INFO pkt\n");
#endif
            YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"Invalid prog packet");
            return -1;
        }
        break;
    }
    return 0;
}




static int uSendCmd(u8 cmd,FLASH_DEVICE_STATE nextState)
{
    if(ypIsSendBootloaderBusy(&firm_dev)) {
        return 0;
    }
    memset(&firm_pkt,0,sizeof(USB_Packet));
    firm_pkt.prog.pkt.type = cmd;
    if(ypSendBootloaderCmd(&firm_dev,&firm_pkt,NULL)<0){
        return -1;
    }
    fctx.stepA = nextState;
    return 0;
}

static int uFlashZone()
{
    u16         datasize;
    //char msg[FLASH_ERRMSG_LEN];

    switch(fctx.zst){
        case FLASH_ZONE_START:
            if(fctx.currzone == fctx.bynHead.v6.ROM_nb_zone + fctx.bynHead.v6.FLA_nb_zone){
                fctx.stepA = FLASH_REBOOT;
                return 0;
            }
            uGetFirmwareBynZone(fctx.zOfs, &fctx.bz);
#if defined(DEBUG_FIRMWARE) && !defined(MICROCHIP_API)
            dbglog("Flash zone %d:%x : %x(%x)\n",fctx.currzone,fctx.zOfs,fctx.bz.addr_page,fctx.bz.len);
#endif
            //fixme YSPRINTF(msg, FLASH_ERRMSG_LEN, "Flash zone %d:%x : %x(%x)",fctx.currzone,fctx.zOfs,fctx.bz.addr_page,fctx.bz.len);
            uLogProgress("Flash zone");
            if((fctx.bz.addr_page % (firm_dev.pr_blk_size*2)) !=0 ) {
                YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"ProgAlign");
                return -1;
            }
            fctx.zOfs += sizeof(byn_zone);
            fctx.zNbInstr = fctx.bz.len/3;
            fctx.stepB    = 0;
            if(fctx.zNbInstr < (u32)firm_dev.pr_blk_size){
                YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"ProgSmall");
                return -1;
            }
            fctx.zst = FLASH_ZONE_PROG;
            //no break on purpose
        case FLASH_ZONE_PROG:
            if(ypIsSendBootloaderBusy(&firm_dev)) {
                return 0;
            }
            memset(&firm_pkt,0,sizeof(USB_Packet));
            firm_pkt.prog.pkt.type = PROG_PROG;
            firm_pkt.prog.pkt.adress_low = DECODE_U16(fctx.bz.addr_page & 0xffff);
            firm_pkt.prog.pkt.addres_high = (fctx.bz.addr_page>>16) & 0xff;
            firm_pkt.prog.pkt.size = (u8) (fctx.zNbInstr < MAX_INSTR_IN_PACKET? fctx.zNbInstr : MAX_INSTR_IN_PACKET) ;

            datasize = firm_pkt.prog.pkt.size*3;
            uGetFirmware(fctx.zOfs, firm_pkt.prog.pkt.data, datasize);
            if(ypSendBootloaderCmd(&firm_dev,&firm_pkt,NULL)<0){
                YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"ProgPkt");
                return -1;
            }

            fctx.zOfs     += datasize;
            fctx.zNbInstr -= firm_pkt.prog.pkt.size;
            fctx.stepB    += firm_pkt.prog.pkt.size;
            fctx.progress = (u16)(4 + 92*fctx.zOfs / fctx.len);

            if( fctx.stepB >= firm_dev.pr_blk_size){
                //look for confirmation
                fctx.timeout =ytime()+ BLOCK_FLASH_TIMEOUT;
                fctx.zst =  FLASH_ZONE_RECV_OK;
            }
            break;
        case FLASH_ZONE_RECV_OK:
            if(ypGetBootloaderReply(&firm_dev, &firm_pkt,NULL)<0){
                if((s32)(fctx.timeout - ytime()) < 0) {
#if defined(DEBUG_FIRMWARE) && !defined(MICROCHIP_API)
                    dbglog("Bootlaoder did not send confirmation for Zone %x Block %x\n",fctx.currzone,fctx.bz.addr_page);
#endif
                    YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"ProgDead");
                    return -1;
                }
                return 0;
            }
            if(firm_pkt.prog.pkt.type != PROG_PROG){
                YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"ProgReply");
                return -1;
            }else{
                u32 newblock = ((u32)firm_pkt.prog.pkt.addres_high <<16) | DECODE_U16(firm_pkt.prog.pkt.adress_low);
                //uLogProgress("Block %x to %x is done",fctx.zStartAddr,newblock);
                fctx.bz.addr_page = newblock;
            }
            fctx.stepB = fctx.stepB - firm_dev.pr_blk_size;
            if(fctx.zNbInstr==0){
                fctx.zst =  FLASH_ZONE_START;
                fctx.currzone++;
            }else{
                fctx.zst =  FLASH_ZONE_PROG;
            }
            break;
        default:
            YASSERT(0);
    }

    return 0;
}



#ifndef MICROCHIP_API

static void uSendReboot(u16 signature, FLASH_DEVICE_STATE nextState)
{
    if(ypIsSendBootloaderBusy(&firm_dev))
        return;
    memset(&firm_pkt,0,sizeof(USB_Packet));
    firm_pkt.prog.pkt_ext.type = PROG_REBOOT;
    firm_pkt.prog.pkt_ext.opt.btsign = DECODE_U16(signature);
    // do not check reboot packet on purpose (most of the time
    // the os generate an error because the device rebooted too quickly)
    ypSendBootloaderCmd(&firm_dev,&firm_pkt,NULL);
    fctx.stepA =  nextState;
    return;
}

static int uSendErase(u16 firstPage, u16 nPages, FLASH_DEVICE_STATE nextState)
{
    if(ypIsSendBootloaderBusy(&firm_dev))
        return 0;
    memset(&firm_pkt,0,sizeof(USB_Packet));
    firm_pkt.prog.pkt_ext.type = PROG_ERASE;
    SET_PROG_POS_PAGENO(firm_pkt.prog.pkt_ext, firstPage, 0);
    firm_pkt.prog.pkt_ext.opt.npages = DECODE_U16(nPages);
    if(ypSendBootloaderCmd(&firm_dev,&firm_pkt,NULL)<0){
        return -1;
    }
    fctx.stepA =  nextState;
    return 0;
}

static int uFlashFlash()
{
    u32 addr, datasize;
    u8  buff[MAX_BYTE_IN_PACKET];
    char msg[FLASH_ERRMSG_LEN];
   u32 pos, pageno;

    switch(fctx.zst){
    case FLASH_ZONE_START:
        if(fctx.currzone == fctx.bynHead.v6.ROM_nb_zone + fctx.bynHead.v6.FLA_nb_zone){
            fctx.stepA = FLASH_AUTOFLASH;
            return 0;
        }
        uGetFirmwareBynZone(fctx.zOfs, &fctx.bz);
        if(fctx.currzone < fctx.bynHead.v6.ROM_nb_zone) {
            fctx.bz.addr_page = (u32)firm_dev.first_code_page * firm_dev.ext_page_size + 3*fctx.bz.addr_page/2;
        } else {
            fctx.bz.addr_page = (u32)firm_dev.first_yfs3_page * firm_dev.ext_page_size + fctx.bz.addr_page;
        }
#ifdef DEBUG_FIRMWARE
        dbglog("Flash zone %d:%x : %x(%x)\n",fctx.currzone,fctx.zOfs,fctx.bz.addr_page,fctx.bz.len);
#endif
        YSPRINTF(msg, FLASH_ERRMSG_LEN, "Flash zone %d:%x : %x(%x)",fctx.currzone,fctx.zOfs,fctx.bz.addr_page,fctx.bz.len);
        uLogProgress(msg);

        if((fctx.bz.addr_page & 1) != 0 || (fctx.bz.len & 1) != 0) {
            dbglog("Prog block not on a word boundary (%d+%d)\n", fctx.bz.addr_page, fctx.bz.len);
            YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"Prog block not on a word boundary");
            return -1;
        }
        fctx.zOfs += sizeof(byn_zone);
        fctx.stepB = 0;
        fctx.zst = FLASH_ZONE_PROG;
        //no break on purpose
    case FLASH_ZONE_PROG:
        if(fctx.bz.len > 0 && fctx.currzone < fctx.bynHead.v6.ROM_nb_zone &&
           fctx.bz.addr_page >= (u32)firm_dev.first_yfs3_page * firm_dev.ext_page_size) {
            // skip end of ROM image past reserved flash zone
#ifdef DEBUG_FIRMWARE
            dbglog("Drop ROM data past firmware boundary (zone %d at offset %x)\n", fctx.currzone, fctx.zOfs);
#endif
            fctx.zOfs += fctx.bz.len;
            fctx.bz.len = 0;
            fctx.zst = FLASH_ZONE_START;
            fctx.currzone++;
            return 0;
        }
        addr = fctx.bz.addr_page + fctx.stepB;
        memset(&firm_pkt,0,sizeof(USB_Packet));

        SET_PROG_POS_PAGENO(firm_pkt.prog.pkt_ext, addr / firm_dev.ext_page_size,  addr >> 2);
        datasize = firm_dev.ext_page_size - (addr & (firm_dev.ext_page_size-1));
        if(datasize > MAX_BYTE_IN_PACKET) {
            datasize = MAX_BYTE_IN_PACKET;
        }
        if(fctx.stepB + datasize > fctx.bz.len) {
            datasize = fctx.bz.len - fctx.stepB;
        }
        YASSERT((datasize & 1) == 0);
        firm_pkt.prog.pkt_ext.size = (u8)(datasize / 2);
        firm_pkt.prog.pkt_ext.type = PROG_PROG;
#ifdef DEBUG_FIRMWARE
        {
            u32 page, pos;
            GET_PROG_POS_PAGENO(firm_pkt.prog.pkt_ext, page,  pos);
            pos *=4;
            dbglog("Flash at %x:%x (%x bytes) found at %x (%x more in zone)\n",page, pos,
              2*firm_pkt.prog.pkt_ext.size, fctx.zOfs, fctx.bz.len);
         }
#endif
        uGetFirmware(fctx.zOfs, firm_pkt.prog.pkt_ext.opt.data, 2*firm_pkt.prog.pkt_ext.size);
        if(ypSendBootloaderCmd(&firm_dev,&firm_pkt,NULL)<0){
            dbglog("Unable to send prog pkt\n");
            YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"Unable to send prog pkt");
            return -1;
        }
        fctx.zOfs  += datasize;
        fctx.stepB += datasize;

        // verify each time we finish a page or a zone
        if((addr & (firm_dev.ext_page_size-1)) + datasize >= firm_dev.ext_page_size || fctx.stepB >= fctx.bz.len) {
            fctx.zOfs -= fctx.stepB; // rewind to check
            fctx.zst = FLASH_ZONE_READ;
        }
        break;
    case FLASH_ZONE_READ:
        // pageno is already set properly
        addr = fctx.bz.addr_page;
        SET_PROG_POS_PAGENO(firm_pkt.prog.pkt_ext, addr / firm_dev.ext_page_size,  addr >> 2);
        firm_pkt.prog.pkt.type = PROG_VERIF;
        if(ypSendBootloaderCmd(&firm_dev,&firm_pkt,NULL)<0){
            dbglog("Unable to send verif pkt\n");
            YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"Unable to send verif pkt");
            return -1;
        }
        fctx.zst =  FLASH_ZONE_RECV_OK;
        fctx.timeout =ytime()+ ZONE_VERIF_TIMEOUT;
        //no break on purpose
    case FLASH_ZONE_RECV_OK:
        if(ypGetBootloaderReply(&firm_dev, &firm_pkt,NULL)<0){
            if((s32)(fctx.timeout - ytime()) < 0) {
#ifdef DEBUG_FIRMWARE
                dbglog("Bootlaoder did not send confirmation for Zone %x Block %x\n",fctx.currzone,fctx.bz.addr_page);
#endif
                YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"Device did not respond to verif pkt");
                return -1;
            }
            return 0;
        }
        if(firm_pkt.prog.pkt.type != PROG_VERIF) {
            dbglog("Invalid verif pkt\n");
            YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"Invalid verif pkt");
            return -1;
        }
        GET_PROG_POS_PAGENO(firm_pkt.prog.pkt_ext, pageno, pos);
#ifdef DEBUG_FIRMWARE
            dbglog("Verif at %x:%x (up to %x bytes)\n",pageno,
                  pos <<2,
                  2*firm_pkt.prog.pkt_ext.size);
#endif
        addr = pageno * firm_dev.ext_page_size + (pos << 2) ;
        YASSERT(addr >= fctx.bz.addr_page);
        if(addr < fctx.bz.addr_page + fctx.stepB) {
            // packet is in verification range
            datasize = 2 * firm_pkt.prog.pkt_ext.size;
            if(addr + datasize >= fctx.bz.addr_page + fctx.stepB) {
                datasize = fctx.bz.addr_page + fctx.stepB - addr;
            }
            uGetFirmware(fctx.zOfs + (addr-fctx.bz.addr_page), buff, (u16)datasize);
            if(memcmp(buff, firm_pkt.prog.pkt_ext.opt.data, datasize) != 0) {
                dbglog("Flash verification failed at %x (%x:%x)\n", addr, pageno, addr);
                YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"Flash verification failed");
                return -1;
            }
#ifdef DEBUG_FIRMWARE
        } else {
            dbglog("Skip verification for block at %x (block ends at %x)\n", addr, fctx.bz.addr_page + fctx.stepB);
#endif
        }
        if((addr & (firm_dev.ext_page_size-1)) + 2 * (u32)firm_pkt.prog.pkt_ext.size < (u32)firm_dev.ext_page_size) {
            // more packets expected (device will dump a whole flash page)
            return 0;
        }
        fctx.zOfs += fctx.stepB;
        fctx.progress = (u16)( 4 + 92*fctx.zOfs / (BYN_HEAD_SIZE_V6 + fctx.bynHead.v6.ROM_total_size + fctx.bynHead.v6.FLA_total_size));
        fctx.bz.addr_page += fctx.stepB;
        fctx.bz.len -= fctx.stepB;
        if(fctx.bz.len > 0 && fctx.currzone < fctx.bynHead.v6.ROM_nb_zone &&
           fctx.bz.addr_page >= (u32)firm_dev.first_yfs3_page * firm_dev.ext_page_size) {
            // skip end of ROM image past reserved flash zone
#ifdef DEBUG_FIRMWARE
            dbglog("Drop ROM data past firmware boundary (zone %d at offset %x)\n", fctx.currzone, fctx.zOfs);
#endif
            fctx.zOfs += fctx.bz.len;
            fctx.bz.len = 0;
        }
        if(fctx.bz.len == 0){
            fctx.zst = FLASH_ZONE_START;
            fctx.currzone++;
#ifdef DEBUG_FIRMWARE
            dbglog("Switch to next zone (zone %d at offset %x)\n", fctx.currzone, fctx.zOfs);
#endif
        } else {
            fctx.zst = FLASH_ZONE_PROG;
            fctx.stepB = 0;
#ifdef DEBUG_FIRMWARE
            dbglog("Continue zone %d at offset %x for %x more bytes\n", fctx.currzone, fctx.zOfs, fctx.bz.len);
#endif
        }
    }

    return 0;
}
#endif


YPROG_RESULT uFlashDevice(void)
{
    byn_head_multi  head;
    int             res;

    if(fctx.stepA != FLASH_FIND_DEV && fctx.stepA != FLASH_DONE) {
        if(ypIsSendBootloaderBusy(&firm_dev)) {
            return YPROG_WAITING;
        }
        if(!Flash_ready()) {
            return YPROG_WAITING;
        }
    }

    switch(fctx.stepA){
    case FLASH_FIND_DEV:
        if(uGetBootloader(fctx.bynHead.h.serial,&firm_dev.iface)<0){
#ifndef MICROCHIP_API
            if((s32)(fctx.timeout - ytime()) >= 0) {
                return YPROG_WAITING;
            }
 #endif
            YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"device not present");
#ifdef DEBUG_FIRMWARE
            ulog("device not present\n");
#endif
            fctx.progress = -1;
            return YPROG_DONE;
        }
        fctx.progress = 2;
        uLogProgress("Device detected");

#if defined(DEBUG_FIRMWARE) && defined(MICROCHIP_API)
        ulog("Bootloader ");
        ulog(fctx.bynHead.h.serial);
        ulog(" on port ");
#ifdef MICROCHIP_API
        ulogU16(firm_dev.iface);
#else
        ulogU16(firm_dev.iface.deviceid);
#endif
        ulog("\n");
#endif
        fctx.stepA = FLASH_CONNECT;
        // no break on purpose
    case FLASH_CONNECT:
#ifndef MICROCHIP_API
        if(YISERR(yyySetup(&firm_dev.iface,NULL))){
            YSTRCPY(fctx.errmsg,FLASH_ERRMSG_LEN,"Unable to open connection to the device");
            fctx.progress = -1;
            return YPROG_DONE;
        }
#endif
        uLogProgress("Device connected");
        fctx.stepA = FLASH_GET_INFO;
        fctx.stepB = 0;
        // no break on purpose
    case FLASH_GET_INFO:
        if(uGetDeviceInfo()<0){
#ifdef DEBUG_FIRMWARE
            ulog("uGetDeviceInfo failed\n");
#endif
            YSTRCPY(fctx.errmsg, FLASH_ERRMSG_LEN, "Unable to get bootloader informations");
            fctx.progress = -1;
            fctx.stepA = FLASH_DISCONNECT;
        }
        fctx.progress = 2;
        break;
    case FLASH_VALIDATE_BYN:
#ifdef DEBUG_FIRMWARE
        ulog("PICDev ");
        ulogU16(firm_dev.devid_model);
        ulog(" detected\n");
#endif
        uGetFirmwareBynHead(&head);
        if(ValidateBynCompat(&head,fctx.len,fctx.bynHead.h.serial,&firm_dev,fctx.errmsg)<0){
#ifdef DEBUG_FIRMWARE
            ulog("ValidateBynCompat failed:");
            ulog(fctx.errmsg);
            ulog("\n");
#endif
            fctx.progress = -1;
            fctx.stepA = FLASH_DISCONNECT;
            break;
        }

        switch(head.h.rev) {
            case BYN_REV_V4:
                fctx.bynHead.v6.ROM_nb_zone = (u8)head.v4.nbzones;
                fctx.bynHead.v6.FLA_nb_zone = 0;
                fctx.currzone = 0;
                fctx.zOfs = BYN_HEAD_SIZE_V4;
                break;
            case BYN_REV_V5:
                fctx.bynHead.v6.ROM_nb_zone = (u8)head.v5.nbzones;
                fctx.bynHead.v6.FLA_nb_zone = 0;
                fctx.currzone = 0;
                fctx.zOfs = BYN_HEAD_SIZE_V5;
                break;
            case BYN_REV_V6:
                fctx.bynHead.v6.ROM_nb_zone = (u8)head.v6.ROM_nb_zone;
                fctx.bynHead.v6.FLA_nb_zone = (u8)head.v6.FLA_nb_zone;
                fctx.currzone = 0;
                fctx.zOfs = BYN_HEAD_SIZE_V6;
                break;
            default:
#ifdef DEBUG_FIRMWARE
                ulog("Unsupported file format (upgrade our VirtualHub)\n");
#endif
                fctx.progress = -1;
                fctx.stepA  = FLASH_DISCONNECT;
                break;
            }
        fctx.progress = 3;
        fctx.stepA = FLASH_ERASE;
#ifdef DEBUG_FIRMWARE
        ulogU16(fctx.bynHead.v6.ROM_nb_zone);
        ulog(" ROM zones to flash\n");
#endif
        break;
    case FLASH_ERASE:
        fctx.zst = FLASH_ZONE_START;
        fctx.stepB = 0;
#ifdef MICROCHIP_API
        res = uSendCmd(PROG_ERASE,FLASH_WAIT_ERASE);
#else
        if(firm_dev.ext_total_pages) {
            res = uSendErase(firm_dev.first_code_page, firm_dev.ext_total_pages - firm_dev.first_code_page, FLASH_WAIT_ERASE);
        } else {
            res = uSendCmd(PROG_ERASE,FLASH_WAIT_ERASE);
        }
#endif
        if(res<0){
#ifdef DEBUG_FIRMWARE
            ulog("FlashErase failed\n");
#endif
            YSTRCPY(fctx.errmsg,sizeof(fctx.errmsg),"Unable to blank flash");
            fctx.stepA = FLASH_DISCONNECT;
        }
        break;
    case FLASH_WAIT_ERASE:
        if(fctx.stepB == 0) {
            fctx.stepB = ytime();
        } else {
            u32 delay =1000 + (firm_dev.last_addr>>6);
            if((u32)(ytime() - fctx.stepB) < delay) {
                return YPROG_WAITING;
            }
            fctx.stepA = FLASH_DOFLASH;
            fctx.stepB = 0;
        }
        break;
    case FLASH_DOFLASH:
#ifdef MICROCHIP_API
        res = uFlashZone();
#else
        if(firm_dev.ext_total_pages) {
            res = uFlashFlash();
        } else {
            res = uFlashZone();
        }
#endif
        if(res<0){
#ifdef DEBUG_FIRMWARE
            ulog("Flash failed\n");
            ulog("errmsg=");
            ulog(fctx.errmsg);
            ulogChar('\n');
#endif
            fctx.progress = -1;
            fctx.stepA = FLASH_DISCONNECT;
        }
        break;
    case FLASH_REBOOT:
        fctx.progress = 98;
#ifdef DEBUG_FIRMWARE
        ulog("Send reboot\n");
#endif
        // do not check reboot packet on purpose (most of the time
        // the os generate an error because the device rebooted too quickly)
        uSendCmd(PROG_REBOOT, FLASH_SUCCEEDED);
        fctx.stepA  = FLASH_SUCCEEDED;
        break;
#ifndef MICROCHIP_API
    case FLASH_AUTOFLASH:
        fctx.progress = 98;
        uSendReboot(START_AUTOFLASHER_SIGN, FLASH_SUCCEEDED);
        fctx.stepA  = FLASH_SUCCEEDED;
        break;
#endif
    case FLASH_SUCCEEDED:
        fctx.progress = 99;
#ifdef DEBUG_FIRMWARE
        ulog("Flash succeeded\n");
#endif
        YSTRCPY(fctx.errmsg,sizeof(fctx.errmsg),"Flash succeeded");
#ifdef MICROCHIP_API
        ypBootloaderShutdown(&firm_dev);
#endif
        fctx.stepA   = FLASH_DISCONNECT;
    case FLASH_DISCONNECT:
#ifdef DEBUG_FIRMWARE
        ulog("Flash disconnect\n");
#endif
#ifndef MICROCHIP_API
        yyyPacketShutdown(&firm_dev.iface);
#endif
        fctx.stepA   = FLASH_DONE;
        // intentionally no break
    case FLASH_DONE:
        if (fctx.progress > 0)
            fctx.progress = 100;
        return YPROG_DONE;
    }
    return YPROG_WAITING;
}







#ifndef YAPI_IN_YDEVICE


typedef enum
{
    FLASH_HUB_STATE = 0u,
    FLASH_HUB_LIST,
    FLASH_HUB_FLASH,
    FLASH_HUB_UPLOAD,
    FLASH_HUB_NONE
} FLASH_HUB_CMD;



static int checkRequestHeader(FLASH_HUB_CMD cmd, const char *devserial, const char* buffer, u32 len, char *errmsg)
{
    int res;
    yJsonStateMachine j;
    // Parse HTTP header
    j.src = buffer;
    j.end = j.src + len;
    j.st = YJSON_HTTP_START;
    if (yJsonParse(&j) != YJSON_PARSE_AVAIL || j.st != YJSON_HTTP_READ_CODE) {
        return YERRMSG(YAPI_IO_ERROR,"Failed to parse HTTP header");
    }
    if (YSTRCMP(j.token, "200")) {
        return YERRMSG(YAPI_IO_ERROR,"Unexpected HTTP return code");
    }
    if (cmd == FLASH_HUB_UPLOAD) {
        return 0;
    }
    if (yJsonParse(&j) != YJSON_PARSE_AVAIL || j.st != YJSON_HTTP_READ_MSG) {
        return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
    }
    if (yJsonParse(&j) != YJSON_PARSE_AVAIL || j.st != YJSON_PARSE_STRUCT) {
        return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
    }
    res = 0;
    while (yJsonParse(&j) == YJSON_PARSE_AVAIL && j.st == YJSON_PARSE_MEMBNAME) {
        switch (cmd){
        case FLASH_HUB_STATE:
            if (!strcmp(j.token, "state")) {
                if (yJsonParse(&j) != YJSON_PARSE_AVAIL) {
                    return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
                }
                if (YSTRCMP(j.token, "valid")) {
                    return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
                } else {
                    res++;
                }
            } else  if (!strcmp(j.token, "firmware")) {
                if (yJsonParse(&j) != YJSON_PARSE_AVAIL) {
                    return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
                }
                if (YSTRNCMP(j.token, devserial,YOCTO_BASE_SERIAL_LEN)) {
                    return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
                } else {
                    res++;
                }
            } else {
                yJsonSkip(&j, 1);
            }
            break;
        case FLASH_HUB_LIST:
            if (!strcmp(j.token, "list")) {
                if (yJsonParse(&j) != YJSON_PARSE_AVAIL || j.st != YJSON_PARSE_ARRAY) {
                    return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
                }
                while (yJsonParse(&j) == YJSON_PARSE_AVAIL && j.st != YJSON_PARSE_ARRAY) {
                    if (!strcmp(j.token, devserial)) {
                        res++;
                        dbglog("%s in list\n", j.token);
                    }
                }
            }
            yJsonSkip(&j, 1);
            break;
        case FLASH_HUB_FLASH:
            yJsonSkip(&j, 1);
            break;
        default:
            yJsonSkip(&j, 1);
            break;
        }
    }

    return res;
}


// Method used to upload a file to the device
static int uploadFirmware(const char *hubserial, u8 *data, u32 data_len, char *errmsg)
{

    char       *p;
    int         buffer_size = 1024 + data_len;
    char        *buffer = yMalloc(buffer_size);
    char        boundary[32];
    int         res;
    YIOHDL      iohdl;
    char    *reply = NULL;
    int     replysize = 0;

    do {
        YSPRINTF(boundary, 32, "Zz%06xzZ", rand() & 0xffffff);
    } while (ymemfind(data, data_len, (u8*)boundary, YSTRLEN(boundary)) >= 0 );

    YSTRCPY(buffer, buffer_size, "POST /upload.html HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=");
    YSTRCAT(buffer, buffer_size, boundary);
    YSTRCAT(buffer, buffer_size,
        "\r\n\r\n"
        "--");
    YSTRCAT(buffer, buffer_size, boundary);
    YSTRCAT(buffer, buffer_size, "\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"api\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Transfer-Encoding: binary\r\n\r\n");
    p = buffer + YSTRLEN(buffer);
    memcpy(p, data, data_len);
    p += data_len;
    YASSERT(p - buffer < buffer_size);
    buffer_size -= (int)(p - buffer);
    YSTRCPY(p, buffer_size, "\r\n--");
    YSTRCAT(p, buffer_size, boundary);
    YSTRCAT(p, buffer_size, "--\r\n");
    buffer_size = (int)(p - buffer) + YSTRLEN(p);
    res = yapiHTTPRequestSyncStartEx(&iohdl, hubserial, buffer, buffer_size, &reply, &replysize, errmsg);
    if (res >= 0) {
        res = checkRequestHeader(FLASH_HUB_UPLOAD, "", reply,replysize, errmsg);
        yapiHTTPRequestSyncDone(&iohdl, errmsg);
    }
    yFree(buffer);
    return res;
}



static int sendHubFlashCmd(const char *hubserial, const char *devserial, FLASH_HUB_CMD cmd, const char *args, char *errmsg)
{
    char buffer[512];
    int res = -1;
    YIOHDL  iohdl;
    char    *reply = NULL;
    int     replysize = 0;
    const char *cmd_str;

    switch (cmd){
    case FLASH_HUB_STATE: cmd_str = "state"; break;
    case FLASH_HUB_LIST: cmd_str = "list"; break;
    case FLASH_HUB_FLASH: cmd_str = "flash"; break;
    default:
        return YERR(YAPI_INVALID_ARGUMENT);
    }

    YSPRINTF(buffer,512,"GET /flash.json?a=%s%s \r\n\r\n",cmd_str,args);
    res = yapiHTTPRequestSyncStart(&iohdl, hubserial, buffer, &reply, &replysize, errmsg);
    if (res < 0) {
        return res;
    }
    res = checkRequestHeader(cmd, devserial, reply, replysize, errmsg);
    yapiHTTPRequestSyncDone(&iohdl, errmsg);

    return res;
}


typedef enum
{
    FLASH_USB = 0u,
    FLASH_HUB,
    FLASH_HUB_SUBDEV,
} FLASH_TYPE;


static int isWebPath(const char *path)
{
    if (YSTRNCMP(path, "http://", 7) == 0){
        return 7;
    } else if (YSTRNCMP(path, "www.yoctopuce.com",17) == 0){
        return 0;
    }
    return -1;
}

static int yDownloadFirmware(const char * url, u8 **out_buffer, char *errmsg)
{
    char host[256];
    u8 *buffer;
    int res, len, ofs, i;
    const char * http_ok = "HTTP/1.1 200 OK";


    for (i = 0; i < 255 && i < YSTRLEN(url) && url[i] != '/'; i++){
        host[i] = url[i];
    }

    if (url[i] != '/'){
        return YERRMSG(YAPI_INVALID_ARGUMENT, "invalid url");
    }
    host[i] = 0;

    //yFifoInit(&(hub->fifo), hub->buffer,sizeof(hub->buffer));
    res = yTcpDownload(host, url+i, &buffer, 10000, errmsg);
    if (res < 0){
        return res;
    }
    if (YSTRNCMP((char*)buffer, http_ok, YSTRLEN(http_ok))) {
        yFree(buffer);
        return YERRMSG(YAPI_IO_ERROR, "Unexpected HTTP return code");
    }

    ofs = ymemfind(buffer, res, (u8*)"\r\n\r\n", 4);
    if (ofs <0) {
        yFree(buffer);
        return YERRMSG(YAPI_IO_ERROR, "Invalid HTTP header");

    }
    ofs += 4;
    len = res - ofs;
    *out_buffer = yMalloc(len);
    memcpy(*out_buffer, buffer + ofs, len);
    yFree(buffer);
    return len;

}


static void* yFirmwareUpdate_thread(void* ctx)
{
    yThread     *thread = (yThread*)ctx;
    YAPI_DEVICE dev;
    int         res;
    char        buffer[256];
    char        subpath[256];
    char        replybuf[512];
    const char* reboot_req = "GET %s%s/api/module/rebootCountdown?rebootCountdown=-1 \r\n\r\n";
    const char* reboot_hub = "GET /api/module/rebootCountdown?rebootCountdown=-1003 \r\n\r\n";
    const char* get_api_fmt = "GET %s%s/api.json \r\n\r\n";
    char        hubserial[YOCTO_SERIAL_LEN];
    char        *reply = NULL;
    int         replysize = 0;
    int         ofs;
    u64         timeout;
    FLASH_TYPE  type = FLASH_USB;
    int         online;

    yThreadSignalStart(thread);
    uSetGlobalProgress(1);

    //1% -> 5%
    uLogProgress("Loading firmware");
    ofs = isWebPath(yContext->fuCtx.firmwarePath);
    if (ofs < 0){
        res = yLoadFirmwareFile(yContext->fuCtx.firmwarePath, &fctx.firmware, fctx.errmsg);
    } else {
        res = yDownloadFirmware(yContext->fuCtx.firmwarePath + ofs, &fctx.firmware, fctx.errmsg);
    }
    if (YISERR(res)) {
        yContext->fuCtx.global_progress = YAPI_IO_ERROR;
        goto exitthread;
    }
    fctx.len = res;
    //copy firmware header into context variable (to have same behaviour as a device)
    memcpy(&fctx.bynHead, fctx.firmware, sizeof(fctx.bynHead));
    YSTRCPY(fctx.bynHead.h.serial, YOCTO_SERIAL_LEN, yContext->fuCtx.serial);
    uSetGlobalProgress(5);

    //5% -> 10%
    uLogProgress("Enter in bootloader");
    dev = wpSearch(yContext->fuCtx.serial);
    if (dev != -1) {
        yUrlRef url;
        int urlres = wpGetDeviceUrl(dev, hubserial, subpath, 256, NULL);
        if (urlres < 0) {
            yContext->fuCtx.global_progress = YAPI_IO_ERROR;
            goto exit_and_free;
        }
        url = wpGetDeviceUrlRef(dev);
        if (yHashGetUrlPort(url, NULL, NULL) == USB_URL) {
            // USB connected device -> reboot it in bootloader
            YSPRINTF(buffer, 512, reboot_req, "", "");
            res = yapiHTTPRequest(hubserial, buffer, replybuf, sizeof(replybuf), NULL, fctx.errmsg);
            if (res < 0) {
                yContext->fuCtx.global_progress = YAPI_IO_ERROR;
                goto exit_and_free;
            }
        } else {
            if (YSTRCMP(hubserial, yContext->fuCtx.serial)) {
                type = FLASH_HUB_SUBDEV;
                YSPRINTF(buffer, 512, reboot_req, "/bySerial/", yContext->fuCtx.serial);
                yapiHTTPRequest(hubserial, buffer, replybuf, sizeof(replybuf), NULL, fctx.errmsg);
                if (res < 0) {
                    yContext->fuCtx.global_progress = YAPI_IO_ERROR;
                    goto exit_and_free;
                }
            } else  {
                type = FLASH_HUB;
            }
        }
    }
    uSetGlobalProgress(10);

    //10% -> 40%
    uLogProgress("Send firmware to bootloader");
    if (type != FLASH_USB){
        // IP connected device -> upload the firmware to the Hub
        res = uploadFirmware(hubserial, fctx.firmware, fctx.len, fctx.errmsg);
        if (res < 0) {
            yContext->fuCtx.global_progress = YAPI_IO_ERROR;
            goto exit_and_free;
        }
        // verify that firmware is correctly uploaded
        res = sendHubFlashCmd(hubserial, yContext->fuCtx.serial, FLASH_HUB_STATE, "", fctx.errmsg);
        if (res < 0) {
            yContext->fuCtx.global_progress = YAPI_IO_ERROR;
            goto exit_and_free;
        }


    }
    uSetGlobalProgress(40);

    //40%-> 80%
    uLogProgress("Flash firmware");
    switch (type){
    case FLASH_USB:
        fctx.timeout = ytime() + YPROG_BOOTLOADER_TIMEOUT;
        do {
            res = uFlashDevice();
            if (res != YPROG_DONE){
                uSetGlobalProgress(40 + fctx.progress/5);
                yApproximateSleep(1);
            }
        } while (res != YPROG_DONE);
        if (fctx.progress<0) {
            yContext->fuCtx.global_progress = fctx.progress;
            goto exit_and_free;
        }
        break;
    case FLASH_HUB:
        // the hub itself -> reboot in autoflash mode
        res = yapiHTTPRequest(yContext->fuCtx.serial, reboot_hub, replybuf, sizeof(replybuf), NULL, fctx.errmsg);
        if (res < 0) {
            yContext->fuCtx.global_progress = YAPI_IO_ERROR;
            goto exit_and_free;
        }
        yApproximateSleep(7000);
        break;
    case FLASH_HUB_SUBDEV:
        // verify that the device is in bootloader
        timeout = yapiGetTickCount() + YPROG_BOOTLOADER_TIMEOUT;
        do {
            res = sendHubFlashCmd(hubserial, yContext->fuCtx.serial, FLASH_HUB_LIST, "", fctx.errmsg);
            if (res < 0) {
                yContext->fuCtx.global_progress = YAPI_IO_ERROR;
                goto exit_and_free;
            } else if (res >0) {
                break;
            }
            // device still rebooting
            yApproximateSleep(10);
        } while (yapiGetTickCount()< timeout);
        if (res <= 0) {
            uLogProgress("Hub did not detect bootloader");
            yContext->fuCtx.global_progress = YAPI_IO_ERROR;
            goto exit_and_free;
        }
        //start flash
        YSPRINTF(buffer, 512, "&s=%s", yContext->fuCtx.serial);
        res = sendHubFlashCmd(hubserial, yContext->fuCtx.serial, FLASH_HUB_FLASH, buffer, fctx.errmsg);
        if (res < 0) {
            yContext->fuCtx.global_progress = YAPI_IO_ERROR;
            goto exit_and_free;
        }
        break;
    }
    uSetGlobalProgress(80);

    //80%-> 98%
    uLogProgress("wait to the device restart");
    online = 0;
    timeout = yapiGetTickCount() + 20000;
    do {
        char suberrmsg[YOCTO_ERRMSG_LEN];
        YIOHDL  iohdl;
        char tmp_errmsg[YOCTO_ERRMSG_LEN];
        res = yapiUpdateDeviceList(1, suberrmsg);
        if (res < 0 && type != FLASH_HUB) {
            memcpy(fctx.errmsg,suberrmsg,YOCTO_ERRMSG_LEN);
            yContext->fuCtx.global_progress = YAPI_IO_ERROR;
            goto exit_and_free;
        }
        if (type == FLASH_HUB_SUBDEV){
            YSPRINTF(buffer, 512, get_api_fmt, "/bySerial/", yContext->fuCtx.serial);
        } else{
            YSPRINTF(buffer, 512, get_api_fmt, "", "");
        }
        res = yapiHTTPRequestSyncStart(&iohdl, hubserial, buffer, &reply, &replysize, tmp_errmsg);

        if (res >= 0) {
            if (checkRequestHeader(FLASH_HUB_NONE,"", reply, replysize, tmp_errmsg) >= 0){
                online = 1;
            }
            yapiHTTPRequestSyncDone(&iohdl, tmp_errmsg);
        }
    } while (!online && yapiGetTickCount()< timeout);
    uSetGlobalProgress(98);


    //&& timeout > yapiGetTickCount());
    if (online){
        uLogProgress("Success");
        uSetGlobalProgress(100);
    } else {
        uLogProgress("Device did not reboot correctly");
        uSetGlobalProgress(-1);
    }

exit_and_free:

    if (fctx.firmware) {
        yFree(fctx.firmware);
        fctx.firmware = NULL;
    }

exitthread:
    yThreadSignalEnd(thread);
    return NULL;
}


static int yStartFirmwareUpdate(const char *serial, const char *firmwarePath, char *msg)
{

    if (yContext->fuCtx.serial)
        yFree(yContext->fuCtx.serial);
    yContext->fuCtx.serial = YSTRDUP(serial);
    if (yContext->fuCtx.firmwarePath)
        yFree(yContext->fuCtx.firmwarePath);
    yContext->fuCtx.firmwarePath = YSTRDUP(firmwarePath);
    yContext->fuCtx.global_progress = 0;
    fctx.firmware = NULL;
    fctx.len = 0;
    fctx.progress = 0;
    fctx.stepA = FLASH_FIND_DEV;
    YSTRNCPY(fctx.bynHead.h.serial, YOCTO_SERIAL_LEN, serial, YOCTO_SERIAL_LEN - 1);
    YSTRCPY(fctx.errmsg, FLASH_ERRMSG_LEN, "Firmware update started");
    YSTRCPY(msg, FLASH_ERRMSG_LEN, fctx.errmsg);
    memset(&yContext->fuCtx.thread, 0, sizeof(yThread));
    //yThreadCreate will not create a new thread if there is already one running
    if (yThreadCreate(&yContext->fuCtx.thread, yFirmwareUpdate_thread, NULL)<0){
        yContext->fuCtx.serial = NULL;
        YSTRCPY(msg, FLASH_ERRMSG_LEN, "Unable to start helper thread");
        return YAPI_IO_ERROR;
    }
    return 0;

}



static YRETCODE yapiCheckFirmwareFile(const char *serial, int current_rev, const char *path, char *buffer, int buffersize, int *fullsize, char *errmsg)
{
    byn_head_multi *head;
    int size, res, file_rev;
    u8  *p;


    size = yLoadFirmwareFile(path, &p, errmsg);
    if (YISERR(size)){
        return size;
    }
    head = (byn_head_multi*) p;

    res = IsValidBynFile(head, size, errmsg);
    if (YISERR(res)) {
        yFree(head);
        return res;
    }

    file_rev = atoi(head->h.firmware);
    if (file_rev > current_rev) {
        int pathsize = YSTRLEN(path) + 1;
        if (fullsize)
            *fullsize = pathsize;
        if (pathsize <= buffersize) {
            YSTRCPY(buffer, buffersize, path);
        }
    } else{
        file_rev = 0;
    }
    yFree(head);
    return file_rev;
}


/***************************************************************************
 * new firmware upgrade API
 **************************************************************************/

static YRETCODE yapiCheckFirmware_r(const char *serial, int current_rev, const char *path, char *buffer, int buffersize, int *fullsize, char *errmsg)
{
    int best_rev = current_rev;
    int pathlen = YSTRLEN(path);
    char abspath[1024];
#ifdef WINDOWS_API
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
#else
    struct dirent *pDirent;
    DIR *pDir;
#endif

#ifdef WINDOWS_API
#else

    pDir = opendir(path);
    if (pDir == NULL) {
        return yapiCheckFirmwareFile(serial, current_rev, path, buffer, buffersize, fullsize, errmsg);
    }
#endif

    if (pathlen == 0 || pathlen >= 1024 - 32) {
        return YERRMSG(YAPI_INVALID_ARGUMENT, "path too long");
    }

    YSTRCPY(abspath, 1024, path);
    if (abspath[pathlen - 1] != '/' && abspath[pathlen - 1] != '\\') {
#ifdef WINDOWS_API
        abspath[pathlen] = '\\';
#else
        abspath[pathlen] = '/';
#endif
        abspath[++pathlen] = 0;
    }


#ifdef WINDOWS_API
    // Find the first file in the directory.
    YSTRCAT(abspath, 1024, "*");
    hFind = FindFirstFileA(abspath, &ffd);
    if (INVALID_HANDLE_VALUE == hFind) {
        return yapiCheckFirmwareFile(serial, current_rev, path, buffer, buffersize, fullsize, errmsg);
    }
    do {
        char *name = ffd.cFileName;
#else
    while ((pDirent = readdir(pDir)) != NULL) {
        char *name = pDirent->d_name;
        struct stat buf;
#endif
        int isdir;
        int frev = 0;

        if (*name == '.')
            continue;
        abspath[pathlen] = 0;
        YSTRCAT(abspath, 1024, name);
#ifdef WINDOWS_API
        isdir = ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
#else
        stat(abspath, &buf);
        isdir = S_ISDIR(buf.st_mode);
#endif
        if (isdir)
        {
            frev = yapiCheckFirmware_r(serial, best_rev, abspath, buffer, buffersize, fullsize, errmsg);
        } else {

            if (YSTRLEN(name) < 32 && YSTRNCMP(name, serial, YOCTO_BASE_SERIAL_LEN) == 0) {
                frev = yapiCheckFirmwareFile(serial, best_rev, abspath, buffer, buffersize, fullsize, errmsg);
            }
        }
        if (frev > 0){
            best_rev = frev;
        }

#ifdef WINDOWS_API
    } while (FindNextFileA(hFind, &ffd) != 0);
#else
    }
    closedir(pDir);
#endif
    return best_rev;
}


static int checkFirmwareFromWeb(const char * serial, char * out_url, int url_max_len, int *fullsize,  char * errmsg)
{
    char request[256];
    u8 *buffer;
    int res = 0, len;
    yJsonStateMachine j;

    //yFifoInit(&(hub->fifo), hub->buffer,sizeof(hub->buffer));
    YSPRINTF(request, 256,"/FR/common/getLastFirmwareLink.php?serial=%s" , serial);
    res = yTcpDownload("www.yoctopuce.com", request, &buffer, 10000, errmsg);
    if (res<0){
        return res;
    }
    // Parse HTTP header
    j.src = (char*)buffer;
    j.end = j.src + res;
    j.st = YJSON_HTTP_START;
    if (yJsonParse(&j) != YJSON_PARSE_AVAIL || j.st != YJSON_HTTP_READ_CODE) {
        yFree(buffer);
        return YERRMSG(YAPI_IO_ERROR,"Unexpected HTTP return code");
    }
    if (YSTRCMP(j.token, "200")) {
        yFree(buffer);
        return YERRMSG(YAPI_IO_ERROR,"Unexpected HTTP return code");
    }
    if (yJsonParse(&j) != YJSON_PARSE_AVAIL || j.st != YJSON_HTTP_READ_MSG) {
        yFree(buffer);
        return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
    }
    if (yJsonParse(&j) != YJSON_PARSE_AVAIL || j.st != YJSON_PARSE_STRUCT) {
        yFree(buffer);
        return YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
    }
    res = 0;
    while (yJsonParse(&j) == YJSON_PARSE_AVAIL && j.st == YJSON_PARSE_MEMBNAME) {
        if (!strcmp(j.token, "link")) {
            if (yJsonParse(&j) != YJSON_PARSE_AVAIL) {
                res = YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
                break;
            }
            len = YSTRLEN(j.token);
            if (fullsize){
                *fullsize = len;
            }
            if (url_max_len < len + 1){
                res = YERRMSG(YAPI_INVALID_ARGUMENT, "buffer too small");
                break;
            }
            if (out_url) {
                YSTRCPY(out_url,url_max_len,j.token);
            }
        } else  if (!strcmp(j.token, "version")) {
            if (yJsonParse(&j) != YJSON_PARSE_AVAIL) {
                res = YERRMSG(YAPI_IO_ERROR, "Unexpected JSON reply format");
                break;
            }
            res = atoi(j.token);
        } else {
            yJsonSkip(&j, 1);
        }
    }

    yFree(buffer);
    return res;

}

YRETCODE YAPI_FUNCTION_EXPORT yapiCheckFirmware(const char *serial, const char *rev, const char *path, char *buffer, int buffersize, int *fullsize, char *errmsg)
{
    int current_rev = 0;
    int best_rev = 0;

    *buffer = 0;
    if (fullsize)
        *fullsize = 0;
    if (*rev!=0)
        current_rev = atoi(rev);

    if (isWebPath(path)>=0) {
        best_rev = checkFirmwareFromWeb(serial, buffer, buffersize, fullsize, errmsg);
    } else{
        best_rev = yapiCheckFirmware_r(serial, current_rev, path, buffer, buffersize, fullsize, errmsg);
    }
    if (best_rev < 0){
        return best_rev;
    }
    if (best_rev <= current_rev) {
        buffer[0] = 0;
        *fullsize = 0;
        return 0;
    }
    return best_rev;
}

YRETCODE YAPI_FUNCTION_EXPORT yapiUpdateFirmware(const char *serial, const char *firmwarePath, int startUpdate, char *msg)
{
    YRETCODE res;
    yEnterCriticalSection(&fctx.cs);
    if (startUpdate) {
        if (yContext->fuCtx.serial == NULL || yContext->fuCtx.firmwarePath == NULL) {
            res = yStartFirmwareUpdate(serial, firmwarePath, msg);
        } else if (YSTRCMP(serial, yContext->fuCtx.serial) || YSTRCMP(firmwarePath, yContext->fuCtx.firmwarePath)){
            if (yContext->fuCtx.global_progress <0 || yContext->fuCtx.global_progress >=100) {
                res = yStartFirmwareUpdate(serial, firmwarePath, msg);
            } else {
                YSTRCPY(msg, FLASH_ERRMSG_LEN, "Last firmware update is not finished");
                res = 0;
            }
        } else {
            YSTRCPY(msg, FLASH_ERRMSG_LEN, fctx.errmsg);
            res = yContext->fuCtx.global_progress;
        }
    } else {
        if (yContext->fuCtx.serial == NULL || yContext->fuCtx.firmwarePath == NULL) {
            YSTRCPY(msg, FLASH_ERRMSG_LEN, "No firmware update pending");
            res = YAPI_INVALID_ARGUMENT;
        } else if (YSTRCMP(serial, yContext->fuCtx.serial) || YSTRCMP(firmwarePath, yContext->fuCtx.firmwarePath)){
            YSTRCPY(msg, FLASH_ERRMSG_LEN, "Last firmware update is not finished");
            res = YAPI_INVALID_ARGUMENT;
        } else {
            YSTRCPY(msg, FLASH_ERRMSG_LEN, fctx.errmsg);
            res = yContext->fuCtx.global_progress;
        }
    }
    yLeaveCriticalSection(&fctx.cs);
    return res;
}

#endif /* USE_TRUNK */
