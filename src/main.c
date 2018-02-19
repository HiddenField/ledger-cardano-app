/*******************************************************************************
*   Ledger Blue
*   (c) 2016 Ledger
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include "os.h"
#include "cx.h"
#include "blake2.h"
#include "blake2-impl.h"
#include "adaBase58.h"

#include "os_io_seproxyhal.h"
#include <string.h>
#include <stdbool.h>

unsigned int io_seproxyhal_touch_exit(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_address_ok(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_address_cancel(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_preview_ok(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_preview_cancel(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_preview_prev(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_preview_next(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_sign_ok(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_sign_cancel(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_signing_completed(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_signing_aborted(const bagl_element_t *e);

size_t cbor_deserialize_array_indefinite(unsigned char *buffer, size_t offset);
bool cbor_at_end(unsigned char *buffer, size_t offset);
bool cbor_at_break(unsigned char *buffer, size_t offset);

unsigned int prepare_tx_preview_ui();

//TODO: Remove - Temp
unsigned int io_seproxyhal_touch_show_preview(const bagl_element_t *e);

#define MAX_BIP32_PATH 10
#define MAX_USER_NAME 20
#define MAX_CHUNK_SIZE 56
#define MAX_TX_SIZE 1023
#define MAX_TX_IO 6
#define MAX_CHAR_PER_ADDR 13
#define MAX_ADDR_OUT_LENGTH 170
#define MAX_ADDR_IN_LENGTH 124

#define ADA_ROOT_BIP32_PATH_LEN 0x02
#define ADA_ADDR_BIP32_PATH_LEN 0x04
#define BIP_44 0x2C
#define ADA_COIN_TYPE 0x717
#define ADA_ACCOUNT 0x00
#define HARDENED_BIP32 0x80000000

#define CLA 0x80
#define INS_GET_PUBLIC_KEY 0x01
#define INS_SET_TX 0x02
#define INS_SIGN_TX 0x03
#define INS_APP_INFO 0x04
#define INS_BLAKE2B_TEST 0x07
#define INS_BASE58_ENCODE_TEST 0x08
#define INS_CBOR_DECODE_TEST 0x09

#define P1_FIRST 0x01
#define P1_NEXT 0x02
#define P1_RECOVERY_PASSPHRASE 0x01
#define P1_ADDRESS_PUB_KEY 0x02
#define P2_SINGLE_TX 0x00
#define P2_MULTI_TX 0x02

#define OFFSET_CLA 0
#define OFFSET_INS 1
#define OFFSET_P1 2
#define OFFSET_P2 3
#define OFFSET_LC 4
#define OFFSET_CDATA 8

#define CBOR_7              0xE0  /* type 7 (float and other types) */
#define CBOR_ARRAY          0x80  /* type 4 */
#define CBOR_VAR_FOLLOWS    31    /* 0x1f */
#define CBOR_BREAK      (CBOR_7 | 31)



unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];

// display stepped screens
unsigned int ux_step;
unsigned int ux_step_count;

ux_state_t ux;

typedef struct operationContext_t {
    uint8_t p1;
    uint8_t p2;
    uint8_t *dataBuffer;
    uint32_t dataOffset;
    uint32_t dataLength;
    uint8_t pathLength;
    unsigned char address_base58[MAX_ADDR_OUT_LENGTH];
    uint32_t bip32Path[MAX_BIP32_PATH];
    cx_ecfp_public_key_t publicKey;
    unsigned char chainCode[32];
    bool isDataReadComplete;
    bool getWalletRecoveryPassphrase;
    uint8_t message[MAX_TX_SIZE];
    uint64_t transactionLength;
    uint8_t finalUTXOCount;
    uint64_t txAmountData[MAX_TX_IO];
    uint8_t hashTX[32];
    #ifdef INS_CBOR_DECODE_TEST_FUNC
    uint32_t addressData[16];
    #endif
} operationContext_t;


char *ui_addresses[MAX_TX_IO][MAX_CHAR_PER_ADDR];
char *ui_address_ptr;
uint8_t raw_address_length;
uint8_t base58_address_length;
#ifdef INS_CBOR_DECODE_TEST_FUNC
uint8_t *checkSumPtr;
#endif
uint8_t *address_start_index;
uint8_t *txAmount;
uint32_t *current_signing_address_i;
uint8_t privateKeyData[32];
cx_ecfp_private_key_t privateKey;
bool is_tx_set;
uint8_t tx_sign_counter;

const char * const ui_strings[3] = {"Send ADA", "To Address", "TX Fee ADA" };

struct {
    char ui_label[32];
    char ui_value[32];
    uint8_t tx_ui_step;
    uint8_t otx_count;
} tx_ui_t;

operationContext_t operationContext;

char ada_print_amount_tmp[32];
char ada_print_amount_tmp_2[32];




const bagl_element_t ui_idle_nanos[] = {
    // type                               userid    x    y   w    h  str rad
    // fill      fg        bg      fid iid  txt   touchparams...       ]
    {{BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x00, 0, 20, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Cardano",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_ICON, 0x00, 3, 3, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CROSS},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
};
unsigned int ui_idle_nanos_button(unsigned int button_mask,
                                  unsigned int button_mask_counter) {
    switch (button_mask) {

        case BUTTON_EVT_RELEASED | BUTTON_LEFT: // EXIT

            // TODO: Wipe TX and all data
            io_seproxyhal_touch_exit(NULL);

            break;
    }
    return 0;
}



const bagl_element_t bagl_ui_sign_tx_nanos[] = {
    // {
    //     {type, userid, x, y, width, height, stroke, radius, fill, fgcolor,
    //      bgcolor, font_id, icon_id},
    //     text,
    //     touch_area_brim,
    //     overfgcolor,
    //     overbgcolor,
    //     tap,
    //     out,
    //     over,
    // },
    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000,
         0xFFFFFF, 0, 0},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 12, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        "Sign",
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 28, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        "Transaction?",
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 0, 12, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_CROSS},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 120, 12, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_CHECK},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
};

unsigned int
bagl_ui_sign_tx_nanos_button(unsigned int button_mask,
                            unsigned int button_mask_counter) {
    switch (button_mask) {

        case BUTTON_EVT_RELEASED | BUTTON_LEFT: // EXIT

            io_seproxyhal_touch_sign_cancel(NULL);

            break;

        case BUTTON_EVT_RELEASED | BUTTON_RIGHT: // APPORVE

            io_seproxyhal_touch_sign_ok(NULL);

            break;
    }
    return 0;
}





const bagl_element_t bagl_ui_preview_tx_nanos[] = {
    // {
    //     {type, userid, x, y, width, height, stroke, radius, fill, fgcolor,
    //      bgcolor, font_id, icon_id},
    //     text,
    //     touch_area_brim,
    //     overfgcolor,
    //     overbgcolor,
    //     tap,
    //     out,
    //     over,
    // },
    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000,
         0xFFFFFF, 0, 0},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 12, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        tx_ui_t.ui_label,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 28, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        tx_ui_t.ui_value,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 0, 4, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_LEFT},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 120, 4, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_RIGHT},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
};

unsigned int
bagl_ui_preview_tx_nanos_button(unsigned int button_mask,
                            unsigned int button_mask_counter) {
    switch (button_mask) {
    case BUTTON_EVT_RELEASED | BUTTON_LEFT:

        io_seproxyhal_touch_preview_prev(NULL);

        break;

    case BUTTON_EVT_RELEASED | BUTTON_RIGHT:

        io_seproxyhal_touch_preview_next(NULL);

        break;
    }
    return 0;
}


const bagl_element_t bagl_ui_approval_preview_tx_nanos[] = {
    // {
    //     {type, userid, x, y, width, height, stroke, radius, fill, fgcolor,
    //      bgcolor, font_id, icon_id},
    //     text,
    //     touch_area_brim,
    //     overfgcolor,
    //     overbgcolor,
    //     tap,
    //     out,
    //     over,
    // },
    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000,
         0xFFFFFF, 0, 0},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 12, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        "Preview",
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 28, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        "Transaction?",
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 0, 12, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_CROSS},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 120, 12, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_CHECK},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
};

unsigned int
bagl_ui_approval_preview_tx_nanos_button(unsigned int button_mask,
                            unsigned int button_mask_counter) {
    switch (button_mask) {

        case BUTTON_EVT_RELEASED | BUTTON_LEFT: // EXIT

            io_seproxyhal_touch_sign_cancel(NULL);

            break;

        case BUTTON_EVT_RELEASED | BUTTON_RIGHT: // APPORVE PREVIEW

            io_seproxyhal_touch_preview_ok(NULL);

            break;
    }
    return 0;
}



const bagl_element_t bagl_ui_signing_tx_nanos[] = {
    // {
    //     {type, userid, x, y, width, height, stroke, radius, fill, fgcolor,
    //      bgcolor, font_id, icon_id},
    //     text,
    //     touch_area_brim,
    //     overfgcolor,
    //     overbgcolor,
    //     tap,
    //     out,
    //     over,
    // },
    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000,
         0xFFFFFF, 0, 0},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 20, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        "Signing...",
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 24, 14, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_LOADING_BADGE},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
};

unsigned int
bagl_ui_signing_tx_nanos_button(unsigned int button_mask,
                            unsigned int button_mask_counter) {
    return 0;
}





const bagl_element_t bagl_ui_signing_completed_nanos[] = {
    // {
    //     {type, userid, x, y, width, height, stroke, radius, fill, fgcolor,
    //      bgcolor, font_id, icon_id},
    //     text,
    //     touch_area_brim,
    //     overfgcolor,
    //     overbgcolor,
    //     tap,
    //     out,
    //     over,
    // },
    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000,
         0xFFFFFF, 0, 0},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 20, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        "Signing Completed",
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 120, 14, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_CHECK},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
};

unsigned int
bagl_ui_signing_completed_nanos_button(unsigned int button_mask,
                            unsigned int button_mask_counter) {
    switch (button_mask) {

        case BUTTON_EVT_RELEASED | BUTTON_RIGHT: // APPORVE
            io_seproxyhal_touch_signing_completed(NULL);
            break;
    }
    return 0;
}




const bagl_element_t bagl_ui_signing_aborted_nanos[] = {
    // {
    //     {type, userid, x, y, width, height, stroke, radius, fill, fgcolor,
    //      bgcolor, font_id, icon_id},
    //     text,
    //     touch_area_brim,
    //     overfgcolor,
    //     overbgcolor,
    //     tap,
    //     out,
    //     over,
    // },
    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000,
         0xFFFFFF, 0, 0},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_LABELINE, 0x00, 0, 20, 128, 16, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
        "Signing Aborted",
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
    {
        {BAGL_ICON, 0x00, 120, 14, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
         BAGL_GLYPH_ICON_CROSS},
        NULL,
        0,
        0,
        0,
        NULL,
        NULL,
        NULL,
    },
};

unsigned int
bagl_ui_signing_aborted_nanos_button(unsigned int button_mask,
                            unsigned int button_mask_counter) {
    switch (button_mask) {

        case BUTTON_EVT_RELEASED | BUTTON_RIGHT: // APPORVE
            io_seproxyhal_touch_signing_aborted(NULL);
            break;
    }
    return 0;
}




const bagl_element_t ui_address_nanos[] = {
    // type                               userid    x    y   w    h  str rad
    // fill      fg        bg      fid iid  txt   touchparams...       ]
    {{BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x01, 0, 12, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Provide",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABELINE, 0x02, 0, 26, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Public Key?",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_ICON, 0x00, 3, 12, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CROSS},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_ICON, 0x00, 117, 13, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CHECK},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
};
unsigned int ui_address_nanos_button(unsigned int button_mask,
                                    unsigned int button_mask_counter) {
   switch (button_mask) {
       case BUTTON_EVT_RELEASED | BUTTON_LEFT: // CANCEL
           io_seproxyhal_touch_address_cancel(NULL);
           break;

       case BUTTON_EVT_RELEASED | BUTTON_RIGHT: { // OK
           io_seproxyhal_touch_address_ok(NULL);
           break;
       }
   }
   return 0;
}


void ui_idle(void) {
    if (os_seph_features() &
        SEPROXYHAL_TAG_SESSION_START_EVENT_FEATURE_SCREEN_BIG) {
        // Ledger Blue not supported
        THROW(0x600C);
    } else {
        UX_DISPLAY(ui_idle_nanos, NULL);
    }
}

bool cbor_at_break(unsigned char *buffer, size_t offset)
{
    return cbor_at_end(buffer, offset) || buffer[offset] == CBOR_BREAK;
}

bool cbor_at_end(unsigned char *buffer, size_t offset)
{
    /* cbor_stream_t::pos points at the next *free* byte, hence the -1 */
    return buffer ? offset >= operationContext.transactionLength - 1 : true;
}

size_t cbor_deserialize_array_indefinite(unsigned char *buffer, size_t offset)
{
    if (buffer[offset] != (CBOR_ARRAY | CBOR_VAR_FOLLOWS)) {
        return 0;
    }

    return 1;
}

void parse_cbor_transaction() {

  uint8_t array_length;
  bool error = false;
  uint8_t itx_count = 0;
  uint8_t otx_index = 0;
  uint32_t offset = 1;

  // Scan through Input TX and ensure they're valid
  if(cbor_deserialize_array_indefinite(operationContext.message, offset) ) {
      offset++;
      while(!cbor_at_break(operationContext.message, offset) && !error) {
          if(itx_count >= MAX_TX_IO) {
              THROW(0x5905);
          }
          // Skip tag
          offset += 4;
          if(operationContext.message[offset] == 0x58) {
              array_length = operationContext.message[++offset];
              // Skip Array Length
              offset += (array_length + 1);
          } else {
              error = true;

              THROW(0x5901);
          }
          itx_count++;
      }
      offset++;
      if(itx_count < 1) {
          THROW(0x5902);
      }
  } else {
      // Invalid TX, must have at least one input
      error = true;
      THROW(0x5902);
  }

  // Assuming this Tx needs to be signed itx_count number of times
  tx_sign_counter = itx_count;

  // Scan through Output TXs
  operationContext.finalUTXOCount = 0;
  if(cbor_deserialize_array_indefinite(operationContext.message, offset) ) {
      offset ++;

      while(!cbor_at_break(operationContext.message, offset) && !error) {
          if(otx_index >= MAX_TX_IO) {
              THROW(0x5906);
          }
          // Skip tag
          offset += 4;
          if(operationContext.message[offset] == 0x58) {

              address_start_index = operationContext.message + offset - 3;

              array_length = operationContext.message[++offset];
              // Skip Array Length
              offset += array_length +1;
              // Skip CBOR int type
              offset++;

              #ifdef INS_CBOR_DECODE_TEST_FUNC
              // Address Checksum
              checkSumPtr = operationContext.message + offset;
              operationContext.addressData[otx_index] =
                  (checkSumPtr[3] << 24) | (checkSumPtr[2] << 16) |
                  (checkSumPtr[1] << 8) | (checkSumPtr[0]);
              #endif

              // End of address at this offset
              offset += 4;

              // Base58 Encode Address
              raw_address_length = array_length + 10;

              os_memset(operationContext.address_base58, 0, MAX_ADDR_OUT_LENGTH);
              base58_address_length = ada_encode_base58(
                address_start_index,
                raw_address_length,
                operationContext.address_base58,
                MAX_ADDR_OUT_LENGTH);

              // Capture address UI here
              // Attempt WITH Base58 Encoding
              ui_address_ptr = ui_addresses[otx_index];
              os_memset(ui_address_ptr, 0, MAX_CHAR_PER_ADDR);
              os_memmove(ui_address_ptr, operationContext.address_base58, 5);
              os_memmove(ui_address_ptr + 5, "...", 3);
              os_memmove(ui_address_ptr + 8,
                         operationContext.address_base58 + base58_address_length - 5,
                         5);
              ui_addresses[otx_index][MAX_CHAR_PER_ADDR] = '\0';

              // Skip CBOR int type
              offset++;

              // Tx Output Amount
              txAmount = operationContext.message + offset;
              operationContext.txAmountData[otx_index] =
                       ((uint64_t)txAmount[7]) |
                       ((uint64_t)txAmount[6] << 8) |
                       ((uint64_t)txAmount[5] << 16) |
                       ((uint64_t)txAmount[4] << 24) |
                       ((uint64_t)txAmount[3] << 32) |
                       ((uint64_t)txAmount[2] << 40) |
                       ((uint64_t)txAmount[1] << 48) |
                       ((uint64_t)txAmount[0] << 56);
              offset += 8;

              otx_index++;

          } else {
              // Invalid Output Tx Format
              error = true;
              THROW(0x5903);
          }
      }
      if(otx_index < 1) {
          THROW(0x5904);
      }
  } else {
      // Invalid TX, missing Outputs array
      error = true;
      THROW(0x5904);
  }

  operationContext.finalUTXOCount = otx_index;

}

bool adjustDecimals(char *src, uint32_t srcLength, char *target,
                    uint32_t targetLength, uint8_t decimals) {
    uint32_t startOffset;
    uint32_t lastZeroOffset = 0;
    uint32_t offset = 0;

    if ((srcLength == 1) && (*src == '0')) {
        if (targetLength < 2) {
            return false;
        }
        target[offset++] = '0';
        target[offset++] = '\0';
        return true;
    }
    if (srcLength <= decimals) {
        uint32_t delta = decimals - srcLength;
        if (targetLength < srcLength + 1 + 2 + delta) {
            return false;
        }
        target[offset++] = '0';
        target[offset++] = '.';
        for (uint32_t i = 0; i < delta; i++) {
            target[offset++] = '0';
        }
        startOffset = offset;
        for (uint32_t i = 0; i < srcLength; i++) {
            target[offset++] = src[i];
        }
        target[offset] = '\0';
    } else {
        uint32_t sourceOffset = 0;
        uint32_t delta = srcLength - decimals;
        if (targetLength < srcLength + 1 + 1) {
            return false;
        }
        while (offset < delta) {
            target[offset++] = src[sourceOffset++];
        }
        if (decimals != 0) {
            target[offset++] = '.';
        }
        startOffset = offset;
        while (sourceOffset < srcLength) {
            target[offset++] = src[sourceOffset++];
        }
        target[offset] = '\0';
    }
    for (uint32_t i = startOffset; i < offset; i++) {
        if (target[i] == '0') {
            if (lastZeroOffset == 0) {
                lastZeroOffset = i;
            }
        } else {
            lastZeroOffset = 0;
        }
    }
    if (lastZeroOffset != 0) {
        target[lastZeroOffset] = '\0';
        if (target[lastZeroOffset - 1] == '.') {
            target[lastZeroOffset - 1] = '\0';
        }
    }
    return true;
}

unsigned short ada_print_amount(uint64_t amount, char *out,
                                uint32_t outlen) {

    os_memset(ada_print_amount_tmp, 0, 32);
    os_memset(ada_print_amount_tmp_2, 0, 32);

    uint32_t numDigits = 0, i;
    uint64_t base = 1;
    while (base <= amount) {
        base *= 10;
        numDigits++;
    }
    if (numDigits > sizeof(ada_print_amount_tmp) - 1) {
        THROW(EXCEPTION);
    }
    base /= 10;
    for (i = 0; i < numDigits; i++) {
        ada_print_amount_tmp[i] = '0' + ((amount / base) % 10);
        base /= 10;
    }
    ada_print_amount_tmp[i] = '\0';
    //strcpy(ada_print_amount_tmp_2, "");
    adjustDecimals(ada_print_amount_tmp, i, ada_print_amount_tmp_2, 32, 6);
    if (strlen(ada_print_amount_tmp_2) < outlen - 1) {
    //if (strlen(ada_print_amount_tmp) < outlen - 1) {
        //strcpy(out, ada_print_amount_tmp_2);
        os_memmove(out, ada_print_amount_tmp_2, 32);
    } else {
        out[0] = '\0';
    }
    return strlen(out);
}

void derive_bip32_node_private_key(uint8_t *privateKeyDataIn) {

  // START Node Derivation
  #if CX_APILEVEL >= 5
      os_perso_derive_node_bip32(
          CX_CURVE_Ed25519,
          operationContext.bip32Path,
          operationContext.pathLength,
          privateKeyDataIn,
          operationContext.chainCode);
  #else
      os_perso_derive_seed_bip32(operationContext.bip32Path,
                                 operationContext.pathLength,
                                 privateKeyDataIn,
                                 operationContext.chainCode);
  #endif
  // END Node Derivation

}

void parse_uint32(uint32_t *value, uint8_t *data) {
    *value = ((uint32_t)data[3]) | ((uint32_t)data[2] << 8) |
             ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

/**
 * Check incoming data length does not exceed message buffer.
 * Copies data to buffer is within range.
 * Throws: 5001 if buffer is exceeded.
 */
void checkAndCopyDataToBuffer() {
    if(operationContext.dataOffset + operationContext.dataLength > (MAX_TX_SIZE + 1))
    {
        THROW(0x5001);
    } else {
        os_memmove(operationContext.message +
                operationContext.dataOffset,
                operationContext.dataBuffer,
                operationContext.dataLength);
        operationContext.dataOffset += operationContext.dataLength;
    }
}

void readHeaderFromAPDU() {
    operationContext.p1 = G_io_apdu_buffer[OFFSET_P1];
    operationContext.p2 = G_io_apdu_buffer[OFFSET_P2];
    operationContext.dataBuffer = G_io_apdu_buffer + OFFSET_CDATA;
    operationContext.dataLength =
        (G_io_apdu_buffer[4] << 24) | (G_io_apdu_buffer[5] << 16) |
        (G_io_apdu_buffer[6] << 8) | (G_io_apdu_buffer[7]);
}

void readDataFromMultiAPDU() {
    // First APDU -
    if (operationContext.p1 == P1_FIRST) {
        // First APDU contains total transaction length
        operationContext.transactionLength = operationContext.dataLength;

        if (operationContext.p2 == P2_MULTI_TX) {
            operationContext.dataLength = MAX_CHUNK_SIZE;
        }

        operationContext.dataOffset = 0;
        operationContext.isDataReadComplete = false;
    } else if (operationContext.p1 != P1_NEXT) {
        THROW(0x5401);
    }

    // Other APDU Packets - Check buffers have space
    checkAndCopyDataToBuffer();

    // Check if we have read all the data for this instuction
    if(operationContext.dataOffset ==
      operationContext.transactionLength
    ) {
        operationContext.isDataReadComplete = true;
    }
}

void hashDataWithBlake2b() {
    int error = blake2b( operationContext.hashTX,
             32,
             operationContext.message,
             operationContext.transactionLength,
             NULL,
             0 );
    if(error == -1) {
        THROW(0x5402);
    } else if (error == -2) {
        THROW(0x5403);
    } else if (error == -3) {
        THROW(0x5404);
    } else if (error == -4) {
        THROW(0x5405);
    } else if(error != 0) {
        THROW(0x5406);
    }
}


void io_exchange_address() {
    uint32_t tx = 0;
    G_io_apdu_buffer[tx++] = 65; // + sizeof(operationContext.chainCode);
    os_memmove(G_io_apdu_buffer + tx, operationContext.publicKey.W, 65);
    tx += 65;

    if(operationContext.getWalletRecoveryPassphrase) {
        // output chain code
        os_memmove(G_io_apdu_buffer + tx,
                   operationContext.chainCode,
                   sizeof(operationContext.chainCode));
        tx += sizeof(operationContext.chainCode);
    }

    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x00;

    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
    ui_idle();
}

void io_exchange_set_tx() {
    uint32_t tx = 0;
    G_io_apdu_buffer[tx++] = tx_sign_counter;
    G_io_apdu_buffer[tx++] = operationContext.finalUTXOCount;

    for (int i=0; i < operationContext.finalUTXOCount; i++ ) {

        os_memmove(G_io_apdu_buffer + tx, &ui_addresses[i], MAX_CHAR_PER_ADDR -1 );
        tx += MAX_CHAR_PER_ADDR - 1;
        os_memmove(G_io_apdu_buffer + tx, &operationContext.txAmountData[i], 8);
        tx += 8;
    }

    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x00;

    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
}

void io_exchange_signing_aborted() {
    uint8_t tx = 0;
    G_io_apdu_buffer[tx++] = 0x500F;
    G_io_apdu_buffer[tx++] = 0x500F;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, 2);
    // Display back the original UX
    ui_idle();
}

void resetSigningTx() {
    tx_ui_t.tx_ui_step = -1;
    os_memset(operationContext.message, 0, MAX_TX_SIZE);
    operationContext.transactionLength = 0;
    os_memset(operationContext.hashTX, 0, 32);
    is_tx_set = false;
    tx_sign_counter = 0;
}

unsigned int abortSigningTxUI() {
    if(is_tx_set) {
        resetSigningTx();

        #ifdef HEADLESS
            // Do not display aborted display
            io_exchange_signing_aborted();
        #else
            UX_DISPLAY(bagl_ui_signing_aborted_nanos, NULL);
        #endif

        return 1;
    }

    return 0;
}

unsigned int io_seproxyhal_touch_exit(const bagl_element_t *e) {
    // Go back to the dashboard
    os_sched_exit(0);
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_show_preview(const bagl_element_t *e) {
    UX_DISPLAY(bagl_ui_approval_preview_tx_nanos, NULL);
    return 0;
}

unsigned int io_seproxyhal_touch_preview_ok(const bagl_element_t *e) {
    tx_ui_t.tx_ui_step = 0;
    tx_ui_t.otx_count = operationContext.finalUTXOCount;

    prepare_tx_preview_ui();

    UX_DISPLAY(bagl_ui_preview_tx_nanos, NULL);

    return 0;
}

unsigned int io_seproxyhal_touch_preview_cancel(const bagl_element_t *e) {

    resetSigningTx();

    ui_idle();

    return 0;
}

unsigned int io_seproxyhal_touch_preview_prev(const bagl_element_t *e) {
    if(tx_ui_t.tx_ui_step > 0) {  // GO BACK

        tx_ui_t.tx_ui_step--;

    } else { // EXIT

        UX_DISPLAY(bagl_ui_approval_preview_tx_nanos, NULL);
        return 0;
    }

    prepare_tx_preview_ui();

    UX_DISPLAY(bagl_ui_preview_tx_nanos, NULL);

    return 0;
}

unsigned int io_seproxyhal_touch_preview_next(const bagl_element_t *e) {

    if(tx_ui_t.tx_ui_step == (tx_ui_t.otx_count * 2)) {  // CONTINUE TO SIGN
        UX_DISPLAY(bagl_ui_sign_tx_nanos, NULL);
        return 0;
    } else {  // SHOW NEXT
        tx_ui_t.tx_ui_step++;
    }

    prepare_tx_preview_ui();

    UX_DISPLAY(bagl_ui_preview_tx_nanos, NULL);

    return 0;
}

unsigned int prepare_tx_preview_ui() {

    uint64_t fee = 0x00000000;

    os_memset(tx_ui_t.ui_label, 0, 32);
    os_memset(tx_ui_t.ui_value, 0, 32);

    int tx_amount_index = tx_ui_t.tx_ui_step/2;
    int tx_address_index = (tx_ui_t.tx_ui_step -1)/2;

    if(tx_ui_t.tx_ui_step == (tx_ui_t.otx_count * 2)) {
        os_memmove(tx_ui_t.ui_label, ui_strings[2], 32);
        ada_print_amount(fee, tx_ui_t.ui_value, 32);
    } else if(tx_ui_t.tx_ui_step % 2 == 0) { // EVEN TX AMOUNT
        os_memmove(tx_ui_t.ui_label, ui_strings[0], 32);
        ada_print_amount(operationContext.txAmountData[tx_amount_index], tx_ui_t.ui_value, 32);
        //ada_print_amount(tx_amount_index, tx_ui_t.ui_value, 31);
        //os_memmove(tx_ui_t.ui_value, &operationContext.txAmountData[tx_amount_index], 32);
        //tx_ui_t.ui_value[31] = '\0';
        //SPRINTF(tx_ui_t.ui_value, "%u", operationContext.txAmountData[tx_amount_index]);
    } else {  // ODD TX ADDRESS
        os_memmove(tx_ui_t.ui_label, ui_strings[1], 32);
        os_memmove(tx_ui_t.ui_value, ui_addresses[tx_address_index], MAX_CHAR_PER_ADDR);

        //ada_print_amount(operationContext.addressData[tx_address_index], tx_ui_t.ui_value, 32);

        //ada_print_amount(ui_addresses[tx_address_index], tx_ui_t.ui_value, MAX_CHAR_PER_ADDR);

        //ada_print_amount(tx_address_index, tx_ui_t.ui_value, 31);
        //os_memmove(tx_ui_t.ui_value, &operationContext.addressData[tx_address_index], 32);
        //tx_ui_t.ui_value[31] = '\0';
        //SPRINTF(tx_ui_t.ui_value, "%u", operationContext.addressData[tx_address_index]);
    }

    return 0;
}


unsigned int io_seproxyhal_touch_sign_ok(const bagl_element_t *e) {

    io_exchange_set_tx();
    #ifdef HEADLESS
        ui_idle();
    #else
        UX_DISPLAY(bagl_ui_signing_tx_nanos, NULL);
    #endif

    return 0;
}

unsigned int io_seproxyhal_touch_sign_cancel(const bagl_element_t *e) {

    //Cleanup transaction data
    resetSigningTx();

    uint32_t tx = 0;
    G_io_apdu_buffer[tx++] = operationContext.finalUTXOCount;
    G_io_apdu_buffer[tx++] = 0x00;

    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x01;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
    // Display back the original UX
    ui_idle();

    return 0;
}

unsigned int io_seproxyhal_touch_address_ok(const bagl_element_t *e) {

    // Send back the response, do not restart the event loop
    io_exchange_address();

    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_address_cancel(const bagl_element_t *e) {
    G_io_apdu_buffer[0] = 0x90;
    G_io_apdu_buffer[1] = 0x01;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, 2);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}


unsigned int io_seproxyhal_touch_signing_completed(const bagl_element_t *e) {
    // Display back the original UX
    ui_idle();

    return 0;
}

unsigned int io_seproxyhal_touch_signing_aborted(const bagl_element_t *e) {

    io_exchange_signing_aborted();
    return 0; // do not redraw the widget

}



unsigned short io_exchange_al(unsigned char channel, unsigned short tx_len) {
    switch (channel & ~(IO_FLAGS)) {
    case CHANNEL_KEYBOARD:
        break;

    // multiplexed io exchange over a SPI channel and TLV encapsulated protocol
    case CHANNEL_SPI:
        if (tx_len) {
            io_seproxyhal_spi_send(G_io_apdu_buffer, tx_len);

            if (channel & IO_RESET_AFTER_REPLIED) {
                reset();
            }
            return 0; // nothing received from the master so far (it's a tx
                      // transaction)
        } else {
            return io_seproxyhal_spi_recv(G_io_apdu_buffer,
                                          sizeof(G_io_apdu_buffer), 0);
        }

    default:
        THROW(INVALID_PARAMETER);
    }
    return 0;
}


void sample_main(void) {
    volatile unsigned int rx = 0;
    volatile unsigned int tx = 0;
    volatile unsigned int flags = 0;

    is_tx_set = false;

    // DESIGN NOTE: the bootloader ignores the way APDU are fetched. The only
    // goal is to retrieve APDU.
    // When APDU are to be fetched from multiple IOs, like NFC+USB+BLE, make
    // sure the io_event is called with a
    // switch event, before the apdu is replied to the bootloader. This avoid
    // APDU injection faults.
    for (;;) {
        volatile unsigned short sw = 0;

        BEGIN_TRY {
            TRY {
                rx = tx;
                tx = 0; // ensure no race in catch_other if io_exchange throws
                        // an error
                rx = io_exchange(CHANNEL_APDU | flags, rx);

                flags = 0;

                // no apdu received, well, reset the session, and reset the
                // bootloader configuration
                if (rx == 0) {
                    THROW(0x6982);
                }

                if (G_io_apdu_buffer[0] != CLA) {
                    THROW(0x6E00);
                }

                if (G_io_apdu_buffer[1] != INS_SIGN_TX &&
                    abortSigningTxUI()) {
                        flags |= IO_ASYNCH_REPLY;
                        THROW(0x500F);
                }


                switch (G_io_apdu_buffer[1]) {

                #ifdef INS_GET_PUBLIC_KEY_FUNC
                case INS_GET_PUBLIC_KEY: {
                    operationContext.p1 = G_io_apdu_buffer[OFFSET_P1];
                    operationContext.p2 = G_io_apdu_buffer[OFFSET_P2];
                    operationContext.dataBuffer = G_io_apdu_buffer + OFFSET_CDATA;

                    if(operationContext.p1 == P1_RECOVERY_PASSPHRASE) {
                        operationContext.getWalletRecoveryPassphrase = true;
                        operationContext.pathLength = ADA_ROOT_BIP32_PATH_LEN;

                        operationContext.bip32Path[0] = BIP_44 |
                                                          HARDENED_BIP32;
                        operationContext.bip32Path[1] = ADA_COIN_TYPE |
                                                          HARDENED_BIP32;

                    } else if (operationContext.p1 == P1_ADDRESS_PUB_KEY) {
                        operationContext.getWalletRecoveryPassphrase = false;
                        operationContext.pathLength = ADA_ADDR_BIP32_PATH_LEN;

                        operationContext.bip32Path[0] = BIP_44 |
                                                          HARDENED_BIP32;
                        operationContext.bip32Path[1] = ADA_COIN_TYPE |
                                                          HARDENED_BIP32;
                        operationContext.bip32Path[2] = ADA_ACCOUNT |
                                                          HARDENED_BIP32;
                        operationContext.bip32Path[3] = (operationContext.dataBuffer[0] << 24) |
                                                        (operationContext.dataBuffer[1] << 16) |
                                                        (operationContext.dataBuffer[2] << 8)  |
                                                        (operationContext.dataBuffer[3]);
                        if(operationContext.bip32Path[3] < HARDENED_BIP32) {
                            //TODO: Set error code
                            THROW(0x5201);
                        }
                    } else {
                        //TODO: Set error code
                        THROW(0x5202);
                    }

                    derive_bip32_node_private_key(privateKeyData);
                    cx_ecfp_init_private_key(CX_CURVE_Ed25519,
                                             privateKeyData,
                                             32,
                                             &privateKey);

                    #if ((CX_APILEVEL >= 5) && (CX_APILEVEL < 7))
                        cx_ecfp_init_public_key(CX_CURVE_Ed25519, NULL, 0,
                                                &operationContext.publicKey);
                        cx_eddsa_get_public_key(&privateKey,
                                                &operationContext.publicKey);
                    #else
                        cx_ecfp_generate_pair(CX_CURVE_Ed25519,
                                              &operationContext.publicKey,
                                              &privateKey, 1);
                    #endif

                    os_memset(&privateKey, 0, sizeof(privateKey));
                    os_memset(privateKeyData, 0, sizeof(privateKeyData));

                    #ifdef HEADLESS
                        io_exchange_address();
                    #else
                        UX_DISPLAY(ui_address_nanos, NULL);
                        flags |= IO_ASYNCH_REPLY;
                    #endif

                }

                break;
                #endif //INS_GET_PUBLIC_KEY_FUNC




                #ifdef INS_BLAKE2B_TEST_FUNC
                case INS_BLAKE2B_TEST: {

                    readHeaderFromAPDU();

                    readDataFromMultiAPDU();

                    if(operationContext.isDataReadComplete) {

                        hashDataWithBlake2b();

                    }

                    uint32_t tx = 0;
                    if(operationContext.isDataReadComplete) {
                        G_io_apdu_buffer[tx++] = 0x20;
                        os_memmove(G_io_apdu_buffer + tx, operationContext.hashTX, 32);
                        tx += 32;
                    }

                    G_io_apdu_buffer[tx++] = 0x90;
                    G_io_apdu_buffer[tx++] = 0x00;
                    // Send back the response, do not restart the event loop
                    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
                    // Display back the original UX
                    ui_idle();
                }

                break;
                #endif //INS_BLAKE2B_TEST_FUNC








                #ifdef INS_SET_TX_FUNC
                case INS_SET_TX: {

                    readHeaderFromAPDU();

                    readDataFromMultiAPDU();

                    if(operationContext.isDataReadComplete) {

                        parse_cbor_transaction();

                        hashDataWithBlake2b();

                        is_tx_set = true;

                        #ifdef HEADLESS
                            io_exchange_set_tx();
                        #else
                            UX_DISPLAY(bagl_ui_approval_preview_tx_nanos, NULL);
                            flags |= IO_ASYNCH_REPLY;
                        #endif

                    } else {
                        uint32_t tx = 0;
                        G_io_apdu_buffer[tx++] = 0x90;
                        G_io_apdu_buffer[tx++] = 0x00;
                        // Send back the response, do not restart the event loop
                        io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
                        // Display back the original UX
                        ui_idle();
                    }

                }

                break;
                #endif //INS_SET_TX_FUNC




                #ifdef INS_SIGN_TX_FUNC
                case INS_SIGN_TX: {

                    // Check Tx and Address Signing Indexes have been set
                    if(!is_tx_set || tx_sign_counter <= 0) {
                        // Reset signing transaction
                        resetSigningTx();
                        // Expecting Tx has been set
                        THROW(0x500E);
                    }

                    // TODO: Check passed in hash equals Tx and Address Index Hash

                    // Header
                    readHeaderFromAPDU();

                    uint32_t address_index;
                    parse_uint32(&address_index, operationContext.dataBuffer);

                    // Set BIP32 ADA path with address index
                    operationContext.pathLength = ADA_ADDR_BIP32_PATH_LEN;
                    operationContext.bip32Path[0] = BIP_44 |
                                                      HARDENED_BIP32;
                    operationContext.bip32Path[1] = ADA_COIN_TYPE |
                                                      HARDENED_BIP32;
                    operationContext.bip32Path[2] = 0 |
                                                      HARDENED_BIP32;
                    operationContext.bip32Path[3] = address_index |
                                                      HARDENED_BIP32;

                    derive_bip32_node_private_key(privateKeyData);

                    cx_ecfp_init_private_key(CX_CURVE_Ed25519,
                                             privateKeyData, 32,
                                             &privateKey);

                    // TODO: Check Tx and Address Signing Indexes have not exchanged


                    uint32_t tx = 0;
                    // Sign TX
                    tx = cx_eddsa_sign(
                        &privateKey, NULL, CX_LAST, CX_SHA512,
                        operationContext.message,
                        operationContext.transactionLength,
                        G_io_apdu_buffer);

                    os_memset(&privateKey, 0, sizeof(privateKey));
                    os_memset(&privateKeyData, 0, sizeof(privateKeyData));

                    G_io_apdu_buffer[tx++] = 0x90;
                    G_io_apdu_buffer[tx++] = 0x00;
                    // Send back the response, do not restart the event loop
                    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
                    // Display back the original UX

                    // Reduce signing counter and check complete
                    tx_sign_counter--;
                    if(tx_sign_counter == 0 ) {
                        resetSigningTx();
                        #ifdef HEADLESS
                            ui_idle();
                        #else
                            UX_DISPLAY(bagl_ui_signing_completed_nanos, NULL);
                            flags |= IO_ASYNCH_REPLY;
                        #endif
                    }

                }
                break;
                #endif //INS_SIGN_TX_FUNC


                #ifdef INS_APP_INFO_FUNC
                case INS_APP_INFO: {

                    uint32_t tx = 0;

                    G_io_apdu_buffer[tx++] = LEDGER_MAJOR_VERSION;
                    G_io_apdu_buffer[tx++] = LEDGER_MINOR_VERSION;
                    G_io_apdu_buffer[tx++] = LEDGER_PATCH_VERSION;

                    G_io_apdu_buffer[tx++] = 0x90;
                    G_io_apdu_buffer[tx++] = 0x00;
                    // Send back the response, do not restart the event loop
                    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
                    // Display back the original UX
                    ui_idle();

                }
                break;
                #endif //INS_APP_INFO_FUNC


                #ifdef INS_CBOR_DECODE_TEST_FUNC
                case INS_CBOR_DECODE_TEST: {

                  readHeaderFromAPDU();

                  readDataFromMultiAPDU();

                  if(operationContext.isDataReadComplete) {
                      parse_cbor_transaction();
                  }

                  uint32_t tx = 0;

                  if(operationContext.isDataReadComplete) {

                      // Output total Tx inputs
                      G_io_apdu_buffer[tx++] = tx_sign_counter;
                      // Output total Tx outputs
                      G_io_apdu_buffer[tx++] = operationContext.finalUTXOCount;

                      for (int i=0; i < operationContext.finalUTXOCount; i++ ) {

                          os_memmove(G_io_apdu_buffer + tx,
                            &operationContext.addressData[i], 4);
                          tx += 4;
                          G_io_apdu_buffer[tx++] = 0xFF;

                          os_memmove(G_io_apdu_buffer + tx,
                            &operationContext.txAmountData[i], 8);
                          tx += 8;
                          G_io_apdu_buffer[tx++] = 0xFF;
                      }
                  }

                  G_io_apdu_buffer[tx++] = 0x90;
                  G_io_apdu_buffer[tx++] = 0x00;
                  // Send back the response, do not restart the event loop
                  io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
                  // Display back the original UX
                  ui_idle();

                }
                break;
                #endif // INS_CBOR_DECODE_TEST_FUNC





                #ifdef INS_BASE58_ENCODE_TEST_FUNC
                case INS_BASE58_ENCODE_TEST: {

                    readHeaderFromAPDU();

                    if(operationContext.dataLength > MAX_ADDR_IN_LENGTH) {
                        THROW(0x5801);
                    }

                    os_memset(operationContext.address_base58, 0, MAX_ADDR_OUT_LENGTH);

                    unsigned char address_length =
                      ada_encode_base58(operationContext.dataBuffer,
                                        operationContext.dataLength,
                                        operationContext.address_base58,
                                        MAX_ADDR_OUT_LENGTH);

                    // Display Address
                    /*
                    os_memset(tx_ui_t.ui_label, 0, 32);
                    os_memset(tx_ui_t.ui_value, 0, 32);
                    os_memmove(tx_ui_t.ui_value, operationContext.address_base58, 5);
                    os_memmove(tx_ui_t.ui_value + 5, "...", 3);
                    tx_ui_t.ui_value[8] = '\0';
                    UX_DISPLAY(bagl_ui_preview_tx_nanos, NULL);
                    flags |= IO_ASYNCH_REPLY;
                    */


                    uint32_t tx = 0;

                    G_io_apdu_buffer[tx++] = address_length;

                    os_memmove(G_io_apdu_buffer + tx, operationContext.address_base58, address_length);
                    tx += address_length;
                    G_io_apdu_buffer[tx++] = 0x90;
                    G_io_apdu_buffer[tx++] = 0x00;
                    // Send back the response, do not restart the event loop
                    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
                    // Display back the original UX
                    ui_idle();

                }

                break;
                #endif //INS_BASE58_ENCODE_TEST_FUNC





                case 0xFF: // return to dashboard
                    os_sched_exit(0);

                default:
                    THROW(0x6D00);
                    break;
                }
            }
            CATCH_OTHER(e) {
                switch (e & 0xF000) {
                case 0x6000:  // System Error Codes
                case 0x5000:  // Application Error Codes
                case 0x9000:  // Success Codes
                    sw = e;
                    break;
                default:
                    sw = 0x6800 | (e & 0x7FF);
                    break;
                }
                // Unexpected exception => report
                G_io_apdu_buffer[tx] = sw >> 8;
                G_io_apdu_buffer[tx + 1] = sw;
                tx += 2;
            }
            FINALLY {
            }
        }
        END_TRY;
    }
}

void io_seproxyhal_display(const bagl_element_t *element) {
    return io_seproxyhal_display_default((bagl_element_t *)element);
}

unsigned char io_event(unsigned char channel) {
    // nothing done with the event, throw an error on the transport layer if
    // needed

    // can't have more than one tag in the reply, not supported yet.
    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT:
        UX_FINGER_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_BUTTON_PUSH_EVENT:
        UX_BUTTON_PUSH_EVENT(G_io_seproxyhal_spi_buffer);
        break;

#ifdef HAVE_BLE
    // Make automatically discoverable again when disconnected

    case SEPROXYHAL_TAG_BLE_CONNECTION_EVENT:
        if (G_io_seproxyhal_spi_buffer[3] == 0) {
            // TODO : cleaner reset sequence
            // first disable BLE before turning it off
            G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_BLE_RADIO_POWER;
            G_io_seproxyhal_spi_buffer[1] = 0;
            G_io_seproxyhal_spi_buffer[2] = 1;
            G_io_seproxyhal_spi_buffer[3] = 0;
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, 4);
            // send BLE power on (default parameters)
            G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_BLE_RADIO_POWER;
            G_io_seproxyhal_spi_buffer[1] = 0;
            G_io_seproxyhal_spi_buffer[2] = 1;
            G_io_seproxyhal_spi_buffer[3] = 3; // ble on & advertise
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, 5);
        }
        break;
#endif

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        UX_DISPLAYED_EVENT({});
        break;

    case SEPROXYHAL_TAG_TICKER_EVENT:
        // prepare next screen
        ux_step = (ux_step + 1) % ux_step_count;
        // redisplay screen
        UX_REDISPLAY();
        break;

    // unknown events are acknowledged
    default:
        break;
    }

    // close the event if not done previously (by a display or whatever)
    if (!io_seproxyhal_spi_is_status_sent()) {
        io_seproxyhal_general_status();
    }
    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

void app_exit(void) {
    BEGIN_TRY_L(exit) {
        TRY_L(exit) {
            os_sched_exit(-1);
        }
        FINALLY_L(exit) {
        }
    }
    END_TRY_L(exit);
}

__attribute__((section(".boot"))) int main(void) {
    // exit critical section
    __asm volatile("cpsie i");

    UX_INIT();

    // ensure exception will work as planned
    os_boot();

    BEGIN_TRY {
        TRY {
            io_seproxyhal_init();

            USB_power(1);

            ui_idle();

            sample_main();
        }
        CATCH_OTHER(e) {
        }
        FINALLY {
        }
    }
    END_TRY;

    app_exit();
}
