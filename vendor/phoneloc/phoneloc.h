#ifndef PHONELOC_H
#define PHONELOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* phone.dat（xluohome 系 / packme v2503）离线归属地库
 * 头：版本(4B ASCII) + 首索引偏移(LE uint32)
 * 记录区：<省份>|<城市>|<邮编>|<区号>\0
 * 索引区：9B/条 = 前7位(LE uint32) + 记录偏移(LE uint32) + 卡型(1B) */

typedef struct {
    char province[24];
    char city[24];
    char zip[10];
    char area_code[8];
    int card_type;
} phoneloc_record;

size_t phoneloc_load(const void* data, size_t size);   /* 返回索引条数，失败 0 */
int phoneloc_query(const char* number, phoneloc_record* rec);
const char* phoneloc_card_name(int card_type);
unsigned phoneloc_version(const void* data);

#ifdef __cplusplus
}
#endif

#endif