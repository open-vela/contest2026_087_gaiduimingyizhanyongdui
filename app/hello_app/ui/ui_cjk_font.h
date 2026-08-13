#ifndef FOCUS_AIOT_UI_CJK_FONT_H
#define FOCUS_AIOT_UI_CJK_FONT_H

#include <stdint.h>

#define UI_CJK_GLYPH_SIZE 14

/* 返回 UTF-32 码点对应的 14x14 点阵；未收录时返回 NULL。 */
const uint16_t *ui_cjk_glyph(uint32_t codepoint);

#endif
