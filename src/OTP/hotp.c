/*
 * Author: Copyright (C) Andrzej Surowiec 2012
 *
 *
 * This file is part of Nitrokey.
 *
 * Nitrokey is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * Nitrokey is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Nitrokey. If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * This file contains modifications done by Rudolf Boeddeker
 * For the modifications applies:
 *
 * Author: Copyright (C) Rudolf Boeddeker  Date: 2013-08-16
 *
 * Nitrokey  is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * Nitrokey is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Nitrokey. If not, see <http://www.gnu.org/licenses/>.
 */


#include <avr32/io.h>
#include <stddef.h>
#include "compiler.h"
#include "flashc.h"
#include "string.h"
#include "time.h"

#include "global.h"
#include "tools.h"

#include "hotp.h"
// #include "memory_ops.h"
// #include "otp_sha1.h"
#include "hmac-sha1.h"
#include "LED_test.h"


/*******************************************************************************

 Local defines

*******************************************************************************/

// #define DEBUG_HOTP
/*
 */

#ifdef DEBUG_HOTP
#else
#define CI_LocalPrintf(...)
#define CI_TickLocalPrintf(...)
#define CI_StringOut(...)
#define CI_Print8BitValue(...)
#define HexPrint(...)
#endif

// #define LITTLE_ENDIAN

#define BIG_ENDIAN  // AVR is BIG_ENDIAN

/*

   Flash storage description OTP

   Stick 1.x

   OTP parameter block

   The OTP parameter block save the configuration data of each slot. The size of each slot is 64 byte

   Slot Start End GLOBAL_CONFIG 0 2 free 3 63 HOTP_SLOT1 64 127 HOTP_SLOT2 128 191 TOTP_SLOT1 192 255 TOTP_SLOT2 256 319 TOTP_SLOT3 320 383
   TOTP_SLOT4 384 447 TOTP_SLOT5 448 511 TOTP_SLOT6 512 575 TOTP_SLOT7 576 639 TOTP_SLOT8 640 703 TOTP_SLOT9 704 767 TOTP_SLOT10 768 831 TOTP_SLOT11
   832 895 TOTP_SLOT12 896 959 TOTP_SLOT13 960 1023 TOTP_SLOT14 1024 1087 TOTP_SLOT15 1088 1151 TOTP_SLOT16 1152 1215


   OTP configuration slot


   Slot size 64 byte

   Contain the parameter data - 50 data byte Start Description SLOT_TYPE_OFFSET 0 1 byte slot type TOTP, HOTP SLOT_NAME_OFFSET 1 15 byte slot name
   SECRET_OFFSET 16 20 byte secret key CONFIG_OFFSET 36 1 byte config byte TOKEN_ID_OFFSET 37 12 byte token ID

   Stick 2.x

   OTP parameter block

   The OTP parameter block save the configuration data of each slot. The size of each slot is 80 byte

   Slot Start End GLOBAL_CONFIG 0 2 free 3 63

   HOTP_SLOT1 64 127 HOTP_SLOT2 128 191 HOTP_SLOT3 192 255

   TOTP_SLOT1 256 335 TOTP_SLOT2 336 415 TOTP_SLOT3 416 495 TOTP_SLOT4 496 575 TOTP_SLOT5 576 655 TOTP_SLOT6 656 735 TOTP_SLOT7 736 815 TOTP_SLOT8
   816 895 TOTP_SLOT9 896 975 TOTP_SLOT10 976 1055 TOTP_SLOT11 1056 1135 TOTP_SLOT12 1136 1215 TOTP_SLOT13 1216 1295 TOTP_SLOT14 1296 1375
   TOTP_SLOT15 1376 1455 TOTP_SLOT16 1456 1535


   Slot size 64 byte

   Contain the parameter data - 50 data byte Start Description SLOT_TYPE_OFFSET 0 1 byte slot type TOTP, HOTP SLOT_NAME_OFFSET 1 15 byte slot name
   SECRET_OFFSET 16 20 byte secret key CONFIG_OFFSET 36 1 byte config byte TOKEN_ID_OFFSET 37 12 byte token ID INTERVAL_OFFSET 50 8 byte (Only HOPT)


   OTP counter storage slot

   This field is used for storing the actual counter value. Because of the limitation of flash erase accesses (1000 for a Stick 1.4 flash page (1024
   byte), 100000 for a Stick 2.0 flash page (512 byte)). it is necessary to reduce the erase access of the flash. This is done by using a greater
   area of the flash. The limitation of flash accesses is only in the change of the bits to 1 in a flash page. Only all bit in a hole flash page can
   set to 1 in a single process (this is the erase access). The setting to 0 of a bit in the flash page is independent to other bits.

   The implementation: The first 8 byte of the slot contains the base counter of stored value as an unsigned 64 bit value. The remaining page stored
   a token per flash byte for a used number. When all tokens in a slot are used, the base counter is raised and the tokens are reseted to 0xff

   Flash page layout

   Entry Position Counter base 0 - 7 8 byte, unsigned 64 bit Use flags 8 - 1023 1016 byte marked a used value (for Stick 1.4) Use flags 8 - 511 504
   byte marked a used value (for Stick 2.0)

   Flash page byte order 0 1 2 3 4 End of flash page 01234567890123456789012345678901234567890123456789.... X
   VVVVVVVVFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF.... .

   V = 64 bit counter base value F = token for a used value, 0xFF = unused, 0x00 = used

   The actual counter is the sum of the counter base value and the number of tokens with a 0 in the slot.

   Example:

   Flash page byte order 0 1 2 3 4 End of flash page 01234567890123456789012345678901234567890123456789.... X xHi
   0000000000000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF.... . xLo 0000001000000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF.... .

   V = 0x0100 = 256 Token 0-7 are marked as used = 8 tokens are marked

   Counter value = 256 + 8 = 264


   Slot size 1024 byte Stick 1.4 Slot size 512 byte Stick 2.0


   OTP backup slot Backup of the OTP parameter block


 */

/*
   u32 hotp_slots[NUMBER_OF_HOTP_SLOTS] = {SLOTS_ADDRESS + HOTP_SLOT1_OFFSET, SLOTS_ADDRESS + HOTP_SLOT2_OFFSET}; u32
   hotp_slot_counters[NUMBER_OF_HOTP_SLOTS] = {SLOT1_COUNTER_ADDRESS, SLOT2_COUNTER_ADDRESS}; u32 hotp_slot_offsets[NUMBER_OF_HOTP_SLOTS] =
   {HOTP_SLOT1_OFFSET, HOTP_SLOT2_OFFSET};

   u32 totp_slots[NUMBER_OF_TOTP_SLOTS] = {SLOTS_ADDRESS + TOTP_SLOT1_OFFSET, SLOTS_ADDRESS + TOTP_SLOT2_OFFSET, SLOTS_ADDRESS + TOTP_SLOT3_OFFSET,
   SLOTS_ADDRESS + TOTP_SLOT4_OFFSET}; u32 totp_slot_offsets[NUMBER_OF_TOTP_SLOTS] = {TOTP_SLOT1_OFFSET, TOTP_SLOT2_OFFSET, TOTP_SLOT3_OFFSET,
   TOTP_SLOT4_OFFSET}; */

u32 hotp_slots[NUMBER_OF_HOTP_SLOTS] = {
    SLOTS_ADDRESS + HOTP_SLOT1_OFFSET,
    SLOTS_ADDRESS + HOTP_SLOT2_OFFSET,
    SLOTS_ADDRESS + HOTP_SLOT3_OFFSET
};

u32 hotp_slot_counters[NUMBER_OF_HOTP_SLOTS] = {
    SLOT1_COUNTER_ADDRESS,
    SLOT2_COUNTER_ADDRESS,
    SLOT3_COUNTER_ADDRESS
};

u32 hotp_slot_offsets[NUMBER_OF_HOTP_SLOTS] = {
    HOTP_SLOT1_OFFSET,
    HOTP_SLOT2_OFFSET,
    HOTP_SLOT3_OFFSET,
};

u32 totp_slots[NUMBER_OF_TOTP_SLOTS + 1] = {
    SLOTS_ADDRESS + TOTP_SLOT1_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT2_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT3_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT4_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT5_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT6_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT7_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT8_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT9_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT10_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT11_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT12_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT13_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT14_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT15_OFFSET,
    SLOTS_ADDRESS + TOTP_SLOT16_OFFSET
};

u32 totp_slot_offsets[NUMBER_OF_TOTP_SLOTS + 1] = {
    TOTP_SLOT1_OFFSET,
    TOTP_SLOT2_OFFSET,
    TOTP_SLOT3_OFFSET,
    TOTP_SLOT4_OFFSET,
    TOTP_SLOT5_OFFSET,
    TOTP_SLOT6_OFFSET,
    TOTP_SLOT7_OFFSET,
    TOTP_SLOT8_OFFSET,
    TOTP_SLOT9_OFFSET,
    TOTP_SLOT10_OFFSET,
    TOTP_SLOT11_OFFSET,
    TOTP_SLOT12_OFFSET,
    TOTP_SLOT13_OFFSET,
    TOTP_SLOT14_OFFSET,
    TOTP_SLOT15_OFFSET,
    TOTP_SLOT16_OFFSET
};

u8 page_buffer[FLASH_PAGE_SIZE * 5];

u64 current_time = 0x0;

/*******************************************************************************

 Global declarations

*******************************************************************************/

/*******************************************************************************

 External declarations

*******************************************************************************/

#define KEYBOARD_FEATURE_COUNT 64

extern u8 HID_GetReport_Value[KEYBOARD_FEATURE_COUNT];

/*******************************************************************************

 Local declarations

*******************************************************************************/

/*******************************************************************************

  getu16

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u16 getu16 (u8 * array)
{
u16 result = array[0] + (array[1] << 8);
    return result;
}


/*******************************************************************************

  getu32

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u32 getu32 (u8 * array)
{
u32 result = 0;
s8 i = 0;


    for (i = 3; i >= 0; i--)
    {
        result <<= 8;
        result += array[i];
    }

    return result;
}


/*******************************************************************************

  getu64

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u64 getu64 (u8 * array)
{
u64 result = 0;
s8 i = 0;

    for (i = 7; i >= 0; i--)
    {
        result <<= 8;
        result += array[i];
    }

    return result;
}


/*******************************************************************************

  endian_swap

  only for little endian systems

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u64 endian_swap (u64 x)
{
#if defined LITTLE_ENDIAN
    x = (x >> 56) |
        ((x << 40) & 0x00FF000000000000LL) |
        ((x << 24) & 0x0000FF0000000000LL) |
        ((x << 8) & 0x000000FF00000000LL) |
        ((x >> 8) & 0x00000000FF000000LL) | ((x >> 24) & 0x0000000000FF0000LL) | ((x >> 40) & 0x000000000000FF00LL) | (x << 56);
#endif
    return x;
}



/*******************************************************************************

  dynamic_truncate

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u32 dynamic_truncate (u8 * hmac_result)
{
u8 offset = hmac_result[19] & 0xf;

u32 bin_code = (hmac_result[offset] & 0x7f) << 24
        | (hmac_result[offset + 1] & 0xff) << 16 | (hmac_result[offset + 2] & 0xff) << 8 | (hmac_result[offset + 3] & 0xff);

    return bin_code;
}

/*******************************************************************************

  crc_STM32

  Changes
  Date      Reviewer        Info
  14.08.14  RB              Integration from Stick 1.4
                              Commit dde179d5d24bd3d06cab73848ba1ab7e5efda898
                              Time in firmware Test #74 Test #105

  Reviews
  Date      Reviewer        Info


*******************************************************************************/

u32 crc_STM32 (u32 time)
{

int i;
u32 value = time << 8;
    // u32 crc;

    for (i = 0; i < 24; i++)
    {
        if (value & 0x80000000)
        {
            value = (value) ^ 0x98800000;   // Polynomial used in STM32
        }
        value = value << 1;
    }

    time = (time << 8) + (value >> 24);

    return time;
}

/*******************************************************************************

  write_data_to_flash

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

void write_data_to_flash (u8 * data, u16 len, u32 addr)
{
    flashc_memcpy ((void *) addr, data, len, TRUE);
}


/*******************************************************************************

  get_hotp_value

  Get a HOTP/TOTP truncated value
  counter - HOTP/TOTP counter value
  secret - pointer to secret stored in memory
  secret_length - length of the secret
  len - length of the truncated result, 6 or 8

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u32 get_hotp_value (u64 counter, u8 * secret, u8 secret_length, u8 len)
{
u8 hmac_result[20];
u64 c = endian_swap (counter);

    LED_GreenOn ();


#ifdef DEBUG_HOTP
    {
u8 text[20];
        CI_LocalPrintf ("output len  :");
        itoa ((u32) len, text);
        CI_LocalPrintf ((s8 *) text);
        CI_LocalPrintf ("\n\r");

        CI_LocalPrintf ("secret len  :");
        itoa ((u32) secret_length, text);
        CI_LocalPrintf (text);
        CI_LocalPrintf ("\n\r");

        CI_LocalPrintf ("counter     :");
        itoa ((u32) counter, text);
        CI_LocalPrintf (text);
        CI_LocalPrintf (" - ");
        HexPrint (8, (u8 *) & counter);
        CI_LocalPrintf ("\n\r");

        CI_LocalPrintf ("secret      :");
        HexPrint (secret_length, secret);
        CI_LocalPrintf ("\n\r");

        CI_LocalPrintf ("c           :");
        HexPrint (8, (u8 *) & c);
        CI_LocalPrintf ("\n\r");
    }
#endif

    hmac_sha1 (hmac_result, secret, secret_length * 8, &c, 64);

#ifdef DEBUG_HOTP
    {
        CI_LocalPrintf ("hmac_result :");
        HexPrint (20, hmac_result);
        CI_LocalPrintf ("\n\r");
    }
#endif

u32 hotp_result = dynamic_truncate (hmac_result);

#ifdef DEBUG_HOTP
    {
u8 text[20];

        CI_LocalPrintf ("hotp_result 1:");
        HexPrint (4, (u8 *) & hotp_result);
        CI_LocalPrintf (" - ");
        itoa (hotp_result, text);
        CI_LocalPrintf (text);


        CI_LocalPrintf ("\n\r");
    }
#endif

    LED_GreenOff ();

    if (len == 6)
        hotp_result = hotp_result % 1000000;
    else if (len == 8)
        hotp_result = hotp_result % 100000000;
    else
        return 0;


#ifdef DEBUG_HOTP
    {
u8 text[20];
        CI_LocalPrintf ("hotp_result 2:");
        HexPrint (4, (u8 *) & hotp_result);
        CI_LocalPrintf (" - ");
        itoa (hotp_result, text);
        CI_LocalPrintf (text);
        CI_LocalPrintf ("\n\r");
    }
#endif

    return hotp_result;
}


/*******************************************************************************

  get_counter_value

  Get the HOTP counter stored in flash
  addr - counter page address

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u64 get_counter_value (u32 addr)
{
u16 i;
u64 counter;
u8* ptr;
u64* ptr_u64;


    ptr_u64 = (u64 *) addr;
    counter = *ptr_u64; // Set the counter base value


    ptr = (u8 *) addr;  // Start of counter storage page
    ptr += 8;   // Start of token area

    i = 0;

    while (i < TOKEN_PER_FLASHPAGE)
    {
        if (*ptr == 0xff)
        {
            break;  // A free token entry found
        }
        ptr++;
        counter++;
        i++;
    }

    return counter;
}


/*******************************************************************************

  get_flash_time_value

  Changes
  Date      Reviewer        Info
  14.08.14  RB              Integration from Stick 1.4
                              Commit dde179d5d24bd3d06cab73848ba1ab7e5efda898
                              Time in firmware Test #74 Test #105

  Reviews
  Date      Reviewer        Info

*******************************************************************************/

u32 get_flash_time_value (void)
{
int i, flag = 0;
u32 time = 0;
u32* p;

    if (getu32 (TIME_ADDRESS) == 0xffffffff)
    {
        return 0xffffffff;
    }

    for (i = 1; i < 32; i++)
    {
        if (getu32 (TIME_ADDRESS + TIME_OFFSET * i) == 0xffffffff)
        {
            p = (TIME_ADDRESS + TIME_OFFSET * (i - 1));
            time = *p;
            flag = 1;
            break;
        }
    }

    if (0 == flag)
    {
        p = TIME_ADDRESS + TIME_OFFSET * 31;
        time = *p;
    }
    /*
       { u8 text[30];

       CI_StringOut ("get_time_value: time "); itoa (time,text); CI_StringOut ((char*)text);

       CI_StringOut (" = "); itoa (crc_STM32 (time>>8),text); CI_StringOut ((char*)text); CI_StringOut ("\r\n"); } */
    return (time);
}


/*******************************************************************************

  get_time_value

  Changes
  Date      Reviewer        Info
  14.08.14  RB              Integration from Stick 1.4
                              Commit dde179d5d24bd3d06cab73848ba1ab7e5efda898
                              Time in firmware Test #74 Test #105

  Reviews
  Date      Reviewer        Info

*******************************************************************************/

u32 get_time_value (void)
{
u32 time = 0;

    time = get_flash_time_value ();

    if (time == 0xffffffff)
    {
        return 0xffffffff;
    }

    if (time != crc_STM32 (time >> 8))
    {
        return (0);
    }
    // uint8_t *ptr=(uint8_t *)TIME_ADDRESS;

    // for (i=0;i<4;i++){
    // time+=*ptr<<(8*i);
    // ptr++;
    // }

    return (time >> 8);
}

/*******************************************************************************

  set_time_value

  Changes
  Date      Reviewer        Info
  14.08.14  RB              Integration from Stick 1.4
                              Commit dde179d5d24bd3d06cab73848ba1ab7e5efda898
                              Time in firmware Test #74 Test #105

  Reviews
  Date      Reviewer        Info

*******************************************************************************/

u8 set_time_value (u32 time)
{
int i, flag = 0;
u32* flash_value;
s32 page_s32;
u32 time_1 = time;

    LED_GreenOn ();

    // Add crc to time
    time = crc_STM32 (time);

    // Find free slot
    for (i = 0; i < 32; i++)
    {
        flash_value = (u32 *) (TIME_ADDRESS + TIME_OFFSET * i);
        if (getu32 ((u8 *) flash_value) == 0xffffffff)  // Is slot free ?
        {
            flashc_memcpy ((void *) (TIME_ADDRESS + TIME_OFFSET * i), (void *) &time, 4, TRUE);
            flag = 1;
            break;
        }
    }

    if (0 == flag)  // Clear page when no free slot was found, an write time
    {
        page_s32 = (TIME_ADDRESS - FLASH_START) / FLASH_PAGE_SIZE;
        flashc_erase_page (page_s32, TRUE);

        flashc_memcpy ((void *) TIME_ADDRESS, (void *) &time, 4, TRUE);
    }

    LED_GreenOff ();

    {
u8 text[30];

        CI_StringOut ("set_time_value: time ");
        itoa (time_1, text);
        CI_StringOut ((char *) text);
        CI_StringOut (" = ");
        itoa (crc_STM32 (time_1), text);
        CI_StringOut ((char *) text);
        CI_StringOut ("\r\n");
    }

    return 0;
}


/*******************************************************************************

  set_counter_value

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u8 set_counter_value (u32 addr, u64 counter)
{
s32 page_s32;

    page_s32 = (addr - FLASH_START) / FLASH_PAGE_SIZE;

    flashc_erase_page (page_s32, TRUE); // clear hole page

    flashc_memcpy ((void *) addr, (void *) &counter, 8, TRUE);  // set the counter base value

    return 0;
}



/*******************************************************************************

  increment_counter_page

  Increment the HOTP counter stored in flash

  addr - counter page address

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u8 increment_counter_page (u32 addr)
{
u16 i;
u8 n;
u16 dummy_u16;
u32 dummy_u32;
u8* ptr;
u64 counter;
FLASH_Status err = FLASH_COMPLETE;

    LED_GreenOn ();

    ptr = (u8 *) addr;  // Set counter page slot

    if (ptr[FLASH_PAGE_SIZE - 1] == 0x00)   // Are all token used ?
    {
        // Entire page is filled, erase cycle
        counter = get_counter_value (addr);

        // Clear the backup page
        flashc_memset8 ((void *) BACKUP_PAGE_ADDRESS, 0xFF, FLASH_PAGE_SIZE, TRUE);

        // write address to backup page
        flashc_memcpy ((void *) BACKUP_PAGE_ADDRESS, (void *) &counter, 8, FALSE);  // Area is erased

        // Write page addr to backup page
        flashc_memcpy ((void *) (BACKUP_PAGE_ADDRESS + BACKUP_ADDRESS_OFFSET), (void *) &addr, 4, FALSE);   // Area is erased


        dummy_u32 = 8;
        flashc_memcpy ((void *) (BACKUP_PAGE_ADDRESS + BACKUP_LENGTH_OFFSET), (void *) &dummy_u32, 4, FALSE);   // Area is erased

        // New erase by flash memset
        flashc_memset8 ((void *) addr, 0xFF, FLASH_PAGE_SIZE, TRUE);    // Erase counter page

        // Write counter page value
        flashc_memcpy ((void *) addr, (void *) &counter, 8, FALSE); // Area is erased

        // Write valid token to backup page
        dummy_u16 = 0x4F4B;
        flashc_memcpy ((void *) (BACKUP_PAGE_ADDRESS + BACKUP_OK_OFFSET), (void *) &dummy_u16, 2, FALSE);   // Area is erased

    }
    else
    {
        ptr += 8;   // Start of token area
        i = 0;

        while (i < TOKEN_PER_FLASHPAGE)
        {
            n = *ptr;
            if (n == 0xff)
            {
                break;  // Token is free
            }
            ptr++;
            i++;
        }

#ifdef DEBUG_HOTP
        {
u8 text[20];

            CI_TickLocalPrintf ("Mark token:");
            itoa ((u32) i, text);
            CI_LocalPrintf (text);
            CI_LocalPrintf (" - Counter:");
            itoa ((u32) get_counter_value (addr), text);
            CI_LocalPrintf (text);
            CI_LocalPrintf ("\n\r");

        }
#endif

        // Mark token as used
        flashc_memset8 ((void *) ptr, 0x00, 1, FALSE);  // Area is erased
        n = *ptr;
        /*
           { u8 text[20];

           CI_LocalPrintf ("After change0:" ); itoa ((u32)n,text); CI_LocalPrintf (text); CI_LocalPrintf ("\n\r"); } */
        if (0 != n) // If token is not erased
        {
            // Mark token as used with erasing page
            flashc_memset8 ((void *) ptr, 0x00, 1, TRUE);
#ifdef DEBUG_HOTP
            {
u8 text[20];
                n = *ptr;
                CI_LocalPrintf ("Token after erase: (0 = ok)");
                itoa ((u32) n, text);
                CI_LocalPrintf (text);
                CI_LocalPrintf ("\n\r");
            }
#endif
        }



    }

    LED_GreenOff ();

    return err; // no error
}


/*******************************************************************************

  get_code_from_hotp_slot

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u32 get_code_from_hotp_slot (u8 slot)
{
u32 result;
u8 len = 6;
u64 counter;
FLASH_Status err;
u8 config = 0;

    if (slot >= NUMBER_OF_HOTP_SLOTS)
        return 0;

    config = get_hotp_slot_config (slot);

    if (config & (1 << SLOT_CONFIG_DIGITS))
        len = 8;

    result = *((u8 *) hotp_slots[slot]);

    if (result == 0xFF) // unprogrammed slot
        return 0;

    LED_GreenOn ();

    counter = get_counter_value (hotp_slot_counters[slot]);
    /*
       For a counter test without flash { static u32 co = 0; counter = co; co++; } */
#ifdef DEBUG_HOTP
    {
u8 text[20];
u32 c;

        DelayMs (10);
        CI_LocalPrintf ("HOTP counter:");
        itoa ((u32) counter, text);
        CI_LocalPrintf (text);
        CI_LocalPrintf ("\n\r");

        CI_LocalPrintf ("secret addr :");
        c = hotp_slots[slot] + SECRET_OFFSET;
        HexPrint (4, &c);
        CI_LocalPrintf ("\n\r");
    }
#endif

    result = get_hotp_value (counter, (u8 *) (hotp_slots[slot] + SECRET_OFFSET), SECRET_LEN, len);


    err = increment_counter_page (hotp_slot_counters[slot]);

#ifdef DEBUG_HOTP
    {
u8 text[20];

        counter = get_counter_value (hotp_slot_counters[slot]);

        CI_LocalPrintf ("HOTP counter after inc:");
        itoa ((u32) counter, text);
        CI_LocalPrintf (text);
        CI_LocalPrintf ("\n\r");
    }
#endif

    LED_GreenOff ();

    if (err != FLASH_COMPLETE)
        return 0;

    return result;
}


/*******************************************************************************

  backup_data

  backup data to the backup page
  data - data to be backed up
  len  - length of the data
  addr - original address of the data

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

void backup_data (u8 * data, u16 len, u32 addr)
{
u16 dummy_u16;
u32 dummy_u32;

    // New erase by flash memset
    flashc_memset8 ((void *) BACKUP_PAGE_ADDRESS, 0xFF, FLASH_PAGE_SIZE, TRUE);

    write_data_to_flash (data, len, BACKUP_PAGE_ADDRESS);

    dummy_u16 = len;
    flashc_memcpy ((void *) (BACKUP_PAGE_ADDRESS + BACKUP_LENGTH_OFFSET), (void *) &dummy_u16, 2, FALSE);   // Area is erased

    dummy_u32 = addr;
    flashc_memcpy ((void *) (BACKUP_PAGE_ADDRESS + BACKUP_ADDRESS_OFFSET), (void *) &dummy_u32, 4, FALSE);  // Area is erased

}

/*******************************************************************************

  erase_counter

  Clear HOTP slot (512 byte)

  Changes
  Date      Reviewer        Info
  14.08.14  RB              Integration from Stick 1.4
                              Commit 3170acd1e68c618aaffd16b2722108c7fc7d5725
                              Secret not overwriten when the Tool sends and empty secret
  Reviews
  Date      Reviewer        Info

*******************************************************************************/

void erase_counter (u8 slot)
{
    LED_GreenOn ();

    flashc_erase_page (hotp_slot_counters[slot], TRUE); // clear hole page

    LED_GreenOff ();

}

/*******************************************************************************

  write_to_slot

  Write a paramter slot to the parameter page

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4
  14.08.14  RB              Integration from Stick 1.4
                              Commit 3170acd1e68c618aaffd16b2722108c7fc7d5725
                              Secret not overwriten when the Tool sends and empty secret
  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

void write_to_slot (u8 * data, u16 offset, u16 len)
{
u16 dummy_u16;
u8* secret;
u8  i;
u8  Found;

    LED_GreenOn ();


    // copy entire page to ram
u8* page = (u8 *) SLOTS_ADDRESS;
    memcpy (page_buffer, page, FLASH_PAGE_SIZE * 5);

    // make changes to page
    memcpy (page_buffer + offset, data, len);

    // check if the secret from the tool is empty and if it is use the old secret
    secret = (u8 *) (data + SECRET_OFFSET);

    // Check if the secret from the tool is empty and if it is use the old secret
    // Secret could begin with 0x00, so checking the whole secret before keeping the old one in mandatory
    Found = FALSE;
    for (i=0;i<SECRET_LEN;i++)
    {
      if (0 != secret[i])
      {
        Found = TRUE;
        break;
      }
    }

    if (FALSE == Found)
    {
        memcpy (data + SECRET_OFFSET, page_buffer + offset + SECRET_OFFSET, SECRET_LEN);
    }

    // write page to backup location
    backup_data (page_buffer, FLASH_PAGE_SIZE * 5, SLOTS_ADDRESS);

    // Clear flash mem
    // flashc_memset8 ((void*)SLOTS_ADDRESS,0xFF,FLASH_PAGE_SIZE*3,TRUE);

    // write page to regular location
    write_data_to_flash (page_buffer, FLASH_PAGE_SIZE * 5, SLOTS_ADDRESS);

    // Init backup block
    dummy_u16 = 0x4F4B;
    flashc_memcpy ((void *) (BACKUP_PAGE_ADDRESS + BACKUP_OK_OFFSET), (void *) &dummy_u16, 2, TRUE);

    LED_GreenOff ();

}


/*******************************************************************************

  check_backups

  check for any data on the backup page

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u8 check_backups ()
{
u32 address = getu32 ((u8 *) BACKUP_PAGE_ADDRESS + BACKUP_ADDRESS_OFFSET);
u16 ok = getu16 ((u8 *) BACKUP_PAGE_ADDRESS + BACKUP_OK_OFFSET);
u16 length = getu16 ((u8 *) BACKUP_PAGE_ADDRESS + BACKUP_LENGTH_OFFSET);

    if (ok == 0x4F4B)   // backed up data was correctly written to its destination
    {
        return 0;
    }
    else
    {

        if ((address != 0xffffffff) && (length <= 1000))    // todo 1000 > define
        {
            // New erase by flash memset
            flashc_memset8 ((void *) address, 0xFF, FLASH_PAGE_SIZE, TRUE);

            write_data_to_flash ((u8 *) BACKUP_PAGE_ADDRESS, length, address);

            // New erase by flash memset
            flashc_memset8 ((void *) BACKUP_PAGE_ADDRESS, 0xFF, FLASH_PAGE_SIZE, TRUE);

            return 1;   // backed up page restored
        }
        else
        {
            return 2;   // something bad happened, but before the original page was earsed, so we're safe (or there is nothing on the backup page)
        }
    }

}


/*******************************************************************************

  get_hotp_slot_config

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u8 get_hotp_slot_config (u8 slot_number)
{
u8 result = 0;
    if (slot_number >= NUMBER_OF_HOTP_SLOTS)
        return 0;
    else
    {
        result = ((u8 *) hotp_slots[slot_number])[CONFIG_OFFSET];
    }

    return result;
}

/*******************************************************************************

  get_totp_slot_config

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info

*******************************************************************************/

u8 get_totp_slot_config (u8 slot_number)
{
u8 result = 0;
    if (slot_number >= NUMBER_OF_TOTP_SLOTS)
        return 0;
    else
    {
        result = ((u8 *) totp_slots[slot_number])[CONFIG_OFFSET];
    }

    return result;
}


/*******************************************************************************

  get_code_from_totp_slot

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u32 get_code_from_totp_slot (u8 slot, u64 challenge)
{
u64 time_min;
u16 interval;
u32 result;
u8 config = 0;
u8 len = 6;
time_t now;

    // Get the local ATMEL time
    time (&now);
    current_time = now;

    if (slot >= NUMBER_OF_TOTP_SLOTS)
        return 0;

    interval = getu16 (totp_slots[slot] + INTERVAL_OFFSET);

    time_min = current_time / interval;

    result = *((u8 *) totp_slots[slot]);
    if (result == 0xFF) // unprogrammed slot
        return 0;


    config = get_totp_slot_config (slot);

    if (config & (1 << SLOT_CONFIG_DIGITS))
        len = 8;

    // result= get_hotp_value(challenge,(u8 *)(totp_slots[slot]+SECRET_OFFSET),20,len);
    result = get_hotp_value (time_min, (u8 *) (totp_slots[slot] + SECRET_OFFSET), SECRET_LEN, len);

    return result;

}

/*******************************************************************************

  get_hotp_slot_addr

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/


u8* get_hotp_slot_addr (u8 slot_number)
{
u8* result = NULL;

    if (slot_number >= NUMBER_OF_HOTP_SLOTS)
    {
        return NULL;
    }
    else
    {
        result = (u8 *) hotp_slots[slot_number];
    }

    return result;
}

/*******************************************************************************

  get_totp_slot_addr

  Changes
  Date      Reviewer        Info
  24.03.14  RB              Integration from Stick 1.4

  Reviews
  Date      Reviewer        Info
  16.08.13  RB              First review

*******************************************************************************/

u8* get_totp_slot_addr (u8 slot_number)
{
u8* result = NULL;

    if (slot_number >= NUMBER_OF_TOTP_SLOTS)
    {
        return NULL;
    }
    else
    {
        result = (u8 *) totp_slots[slot_number];
    }

    return result;
}
