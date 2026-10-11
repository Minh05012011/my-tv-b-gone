#pragma once
/*
  codes_builtin.h
  ----------------------------------------------------------------------------
  Bo ma IR "hat giong" cho TV-B-GONE, phat qua irsend.send(protocol, value, bits).
  
  Da bo sung cac hang TV pho bien tai Viet Nam:
    - Samsung, LG, Sony, Panasonic, Sharp, TCL, Xiaomi, Hisense,
      Skyworth, Toshiba, Philips, JVC, Sanyo, Asanzo, Coocaa,
      Casper, Darling, Aqua, Kangaroo, Viewsonic
      
  Nguon tham khao: IRremoteESP8266 examples, community IR databases,
  dataset pho bien tren GitHub/IRDB.
  
  LUU Y: Day la ma POWER ON/OFF pho bien nhat. Mot so TV doi moi co the
  su dung ma khac. Nen dung che do "LUU IR MOI" de bat ma chinh xac tu
  remote that cua ban.
*/

#include <Arduino.h>
#include <IRsend.h>

struct TvCode {
  const char* brand;
  decode_type_t protocol;
  uint64_t value;
  uint16_t bits;
};

const TvCode BUILTIN_CODES[] = {
  // ===== SAMSUNG (pho bien nhat VN) =====
  {"Samsung",           SAMSUNG,   0xE0E040BF, 32},  // Power toggle pho bien
  {"Samsung (var 2)",   SAMSUNG,   0xE0E019E6, 32},  // Power off discrete
  {"Samsung (var 3)",   SAMSUNG,   0xE0E009F6, 32},  // Power on discrete
  {"Samsung Smart",     SAMSUNG36, 0xE0E040BF0LL, 36},

  // ===== LG (pho bien tai VN) =====
  {"LG",                LG,        0x20DF10EF, 28},  // Power toggle
  {"LG (var 2)",        LG,        0x20DF23DC, 28},
  {"LG (NEC)",          NEC,       0x20DF10EF, 32},
  {"LG OLED",           NEC,       0x55AA1CE3, 32},
  {"LG Smart TV",       NEC,       0x20DF10EF, 32},

  // ===== SONY =====
  {"Sony (12-bit)",     SONY,      0xA90,       12},
  {"Sony (15-bit)",     SONY,      0x140042,    15},
  {"Sony (20-bit)",     SONY,      0x00A8BDB,   20},
  {"Sony Bravia",       SONY,      0x0540C,     15},

  // ===== PANASONIC (Viera - ban nhieu tai VN) =====
  {"Panasonic",         PANASONIC, 0x400401BC,  48},
  {"Panasonic (var 2)", PANASONIC, 0x400401BC24, 48},
  {"Panasonic Viera",   PANASONIC, 0x40040100BCBD, 48},

  // ===== TCL (ban nhieu tai VN, co Alcatel/TCL) =====
  {"TCL",               NEC,       0x00FD40BF,  32},
  {"TCL (var 2)",       NEC,       0x4B36847B,  32},
  {"TCL Roku",          NEC,       0x57E3748B,  32},

  // ===== XIAOMI (Mi TV - ban nhieu tai VN) =====
  {"Xiaomi Mi TV",      NEC,       0x4D7A5DA2,  32},
  {"Xiaomi Mi TV 2",    NEC,       0x77E1A8E1,  32},

  // ===== HISENSE (Vidaa - ban nhieu tai VN) =====
  {"Hisense",           NEC,       0x00E0B877,  32},
  {"Hisense (var 2)",   NEC,       0x80BF13EC,  32},
  {"Hisense (var 3)",   RC6,       0x800F0413,  20},

  // ===== TOSHIBA =====
  {"Toshiba",           NEC,       0x02FD48B7,  32},
  {"Toshiba (var 2)",   NEC,       0x57E308F7,  32},

  // ===== SHARP =====
  {"Sharp",             SHARP,     0x6A,        15},
  {"Sharp (var 2)",     SHARP,     0xAA,        15},
  {"Sharp Aquos",       NEC,       0xA15EC30F,  32},

  // ===== PHILIPS =====
  {"Philips",           RC5,       0x0C,        12},
  {"Philips (RC6)",     RC6,       0x800F0413,  20},
  {"Philips Ambilight", RC6,       0x800F040C,  20},

  // ===== JVC =====
  {"JVC",               JVC,       0xC5E8,      16},
  {"JVC (var 2)",       JVC,       0x4DC813EC,  32},

  // ===== SANYO =====
  {"Sanyo",             SANYO,     0x4CB34CB3,  42},
  {"Sanyo (NEC)",       NEC,       0x1CE32CD3,  32},

  // ===== SKYWORTH (ban o VN) =====
  {"Skyworth",          NEC,       0x807F08F7,  32},
  {"Skyworth (var 2)",  NEC,       0x40BF40BF,  32},

  // ===== COOCAA (Skyworth sub-brand, pho bien VN) =====
  {"Coocaa",            NEC,       0x4DB24D12,  32},

  // ===== ASANZO (hang noi dia VN) =====
  {"Asanzo",            NEC,       0x00FD00FF,  32},
  {"Asanzo (var 2)",    NEC,       0x807F40BF,  32},

  // ===== CASPER (pho bien VN) =====
  {"Casper",            NEC,       0x10EF08F7,  32},
  {"Casper (var 2)",    NEC,       0x4CB3E817,  32},

  // ===== AQUA (hang ban tai VN) =====
  {"Aqua",              NEC,       0x00FF40BF,  32},

  // ===== DARLING (hang noi dia VN) =====
  {"Darling",           NEC,       0x807F18E7,  32},

  // ===== KANGAROO (hang noi dia VN) =====
  {"Kangaroo",          NEC,       0xE0E040BF,  32},

  // ===== VIEWSONIC =====
  {"Viewsonic",         NEC,       0x55AA847B,  32},

  // ===== VIZIO =====
  {"Vizio",             NEC,       0x20DF10EF,  32},

  // ===== HITACHI =====
  {"Hitachi",           NEC,       0x02FD609F,  32},

  // ===== NEC GENERIC (nhieu TV clone tai VN) =====
  {"NEC Generic",       NEC,       0x00FF00FF,  32},
  {"NEC (var 2)",       NEC,       0x00FD00FF,  32},
  {"NEC (var 3)",       NEC,       0xFF00FF00,  32},

  // ===== RC5/RC6 CHAU AU =====
  {"RC5 (chau Au)",     RC5,       0x0C,        12},
  {"RC6",               RC6,       0x800F0410,  20},
};

const size_t BUILTIN_CODES_COUNT = sizeof(BUILTIN_CODES) / sizeof(BUILTIN_CODES[0]);
