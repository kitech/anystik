#include "phoneloc.h"
#include <stdint.h>
#include <string.h>

#define INDEX_ENTRY 9u

static const uint8_t* g_data = NULL;
static size_t g_size = 0;
static uint32_t g_first_index = 0;
static size_t g_index_count = 0;

static uint32_t rd_le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t phoneloc_load(const void* data, size_t size)
{
    g_data = (const uint8_t*)data;
    g_size = size;
    g_index_count = 0;
    if (!data || size < 8)
        return 0;
    g_first_index = rd_le32(g_data + 4);
    if (g_first_index < 8 || g_first_index > size)
        return 0;
    if ((size - g_first_index) % INDEX_ENTRY != 0)
        return 0;
    g_index_count = (size - g_first_index) / INDEX_ENTRY;
    return g_index_count;
}

int phoneloc_query(const char* number, phoneloc_record* rec)
{
    if (!g_data || !rec || !number || g_index_count == 0)
        return 0;

    uint32_t prefix = 0;
    int n = 0;
    for (; n < 7 && number[n] >= '0' && number[n] <= '9'; ++n)
        prefix = prefix * 10 + (uint32_t)(number[n] - '0');
    if (n < 7)
        return 0;

    size_t lo = 0, hi = g_index_count;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const uint8_t* e = g_data + g_first_index + mid * INDEX_ENTRY;
        const uint32_t p = rd_le32(e);
        if (p < prefix)
            lo = mid + 1;
        else if (p > prefix)
            hi = mid;
        else {
            const uint32_t off = rd_le32(e + 4);
            if (off >= g_size)
                return 0;
            const uint8_t* r = g_data + off;
            size_t len = 0;
            while (r + len < g_data + g_size && r[len] != 0)
                ++len;

            memset(rec, 0, sizeof(*rec));
            const uint8_t* seg = r;
            size_t seg_len = 0;
            int field = 0;
            for (size_t i = 0; i <= len; ++i) {
                if (i == len || r[i] == '|') {
                    char* dst = NULL;
                    size_t cap = 0;
                    switch (field) {
                    case 0: dst = rec->province; cap = sizeof rec->province; break;
                    case 1: dst = rec->city;     cap = sizeof rec->city; break;
                    case 2: dst = rec->zip;      cap = sizeof rec->zip; break;
                    case 3: dst = rec->area_code;cap = sizeof rec->area_code; break;
                    default: break;
                    }
                    if (dst) {
                        const size_t c = seg_len < cap - 1 ? seg_len : cap - 1;
                        memcpy(dst, seg, c);
                        dst[c] = '\0';
                    }
                    ++field;
                    seg = r + i + 1;
                    seg_len = 0;
                } else {
                    ++seg_len;
                }
            }
            rec->card_type = e[8];
            return 1;
        }
    }
    return 0;
}

const char* phoneloc_card_name(int t)
{
    switch (t) {
    case 1:  return "中国移动";
    case 2:  return "中国联通";
    case 3:  return "中国电信";
    case 4:  return "中国电信虚拟";
    case 5:  return "中国联通虚拟";
    case 6:  return "中国移动虚拟";
    case 7:  return "中国广电";
    case 8:  return "中国广电虚拟";
    case 20: return "电信物联网";
    case 21: return "联通物联网";
    case 22: return "移动物联网";
    case 23: return "电信数据卡";
    case 24: return "联通数据卡";
    case 25: return "移动数据卡";
    case 26: return "电信卫星电话";
    case 27: return "联通卫星电话";
    case 28: return "移动卫星电话";
    case 29: return "应急通讯卫星";
    case 30: return "工信卫星";
    default: return "未知";
    }
}

unsigned phoneloc_version(const void* data)
{
    if (!data)
        return 0;
    const uint8_t* p = (const uint8_t*)data;
    unsigned v = 0;
    for (int i = 0; i < 4 && p[i] >= '0' && p[i] <= '9'; ++i)
        v = v * 10 + (unsigned)(p[i] - '0');
    return v;
}