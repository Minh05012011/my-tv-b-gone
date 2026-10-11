#pragma once
/*
  app_types.h
  ----------------------------------------------------------------------------
  Chua cac enum/struct dung chung toan project.
  
  CHE DO (MENU CHINH):
    0 = TV-B-GONE   : sub-menu chon che do ban ma
    1 = PHAT IR LUU : duyet & phat lai ma da luu
    2 = LUU IR MOI  : hoc ma tu remote that
    3 = WIFI WEB    : bat AP + web quan ly
    4 = SETTINGS    : sub-menu cai dat (chon chan IR, LED WS2812B on/off)
    5 = LG HACK     : (de trong - cho tinh nang tuong lai)
*/

enum AppMode {
  MENU,
  // Sub-menu TV-B-Gone
  TVBGONE_SUBMENU,
  MODE_TVBGONE_SAVED,    // 1 phat: ma da luu + builtin
  MODE_TVBGONE_WORLD,    // 2 phat: tat ca WORLD_IR_CODES
  // Che do chinh
  MODE_PLAYBACK,
  MODE_LEARN,
  MODE_WIFI,
  MODE_IR_JAMMER, 
  // Sub-menu Settings
  SETTINGS_SUBMENU,
  MODE_TXPIN,
  MODE_LED_CONTROL,
  // LG Hack
  MODE_LGHACK
};

enum ButtonEvent { EVT_NONE, EVT_SHORT, EVT_LONG, EVT_VLONG };
