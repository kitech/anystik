# phoneloc（phone.dat 离线号码归属地查询）

自写 C 解析器：以整个 `phone.dat` 为输入，二分查询手机号段归属地与运营商卡型。
兼容 xluohome 系 / packme 的 `phone.2503.dat` 文件格式（无 IO，数据由调用方读入内存）。

## 数据文件下载地址

- 项目主页（打包工具 `phone_pack.py` + 数据）：https://github.com/20326/packme
- **直链（phone.2503.dat，4,652,693 B，515,858 条，版本 2503=2025年03月）**：
  https://raw.githubusercontent.com/20326/packme/master/phone.2503.dat
- 网络备选（本机直连 raw.githubusercontent 超时，加前缀走代理）：
  https://gh-proxy.com/https://raw.githubusercontent.com/20326/packme/master/phone.2503.dat

说明：

- 数据按月发版，文件名带版本号 `25xx`（`2503` = 2025 年 03 月），格式不变，升级只需替换文件；
  用 `phoneloc_version()` 可从文件头 4 字节读出版本号。
- 同格式的其它 xluohome 系 `phone.dat`（如 xluohome/phonedata、ls0f/phone、pangongzi/phone）可直接互换。
- 数据为第三方从公开信息整理，官方声明不保证绝对正确，生产使用前请自行抽样校对。

## 文件格式

```
| 4 B |  版本号（ASCII 数字，如 "2503"）
------   首个索引偏移（LE uint32）
| 4 B |
------   记录区：<省份>|<城市>|<邮编>|<区号>\0   （每条以 \0 结束）
--------------- 索引区：9 B/条
         <手机号前7位>(LE uint32) | <记录区偏移>(LE uint32) | <卡型>(1 B)
```

解析：读头 8 B 得首个索引偏移 → 索引区按 9 B/条等分 → 二分查找前 7 位 → 取记录偏移读字段。

卡型（`phoneloc_card_name`）：

| 值 | 名称 | 值 | 名称 | 值 | 名称 |
|----|------|----|------|----|------|
| 1  | 中国移动 | 2 | 中国联通 | 3 | 中国电信 |
| 4  | 中国电信虚拟 | 5 | 中国联通虚拟 | 6 | 中国移动虚拟 |
| 7  | 中国广电 | 8 | 中国广电虚拟 | 20-25 | 物联网 / 数据卡（三大运营商） |
| 26-28 | 卫星电话（三大运营商） | 29 | 应急通讯卫星 | 30 | 工信卫星 |
| 其它/99 | 未知 | | | | |

## API

`phoneloc.h`（已 `extern "C"` 包裹，C/C++ 均可直接包含）：

- `size_t phoneloc_load(const void* data, size_t size)`：校验并载入整块数据，返回索引条数，失败返回 0。
  **只存指针不拷贝**，调用方必须保证缓冲区在查询期间存活；单例全局态，重复调用会覆盖上次载入。
- `int phoneloc_query(const char* number, phoneloc_record* rec)`：号码取前 7 位 ASCII 数字二分查找；
  命中返回 1 并填充 `rec`（province/city[24]、zip[10]、area_code[8]、card_type，超长截断；命中时自清零），未命中/非法输入返回 0。
- `const char* phoneloc_card_name(int card_type)`：卡型 → 中文名（未知返回 "未知"）。
- `unsigned phoneloc_version(const void* data)`：读文件头前 4 个 ASCII 数字为版本号（如 2503）。

loader 不负责读文件：应用自行把 `phone.dat` 读入内存后调用 `phoneloc_load`。

## 构建接入

- qmake：`stikcommon.pri` → `myvendor/myvendor.pri` 的 `MYVENDOR_SOURCES` / `MYVENDOR_INCLUDES`（已指向 `$$VENDOR_DIR/phoneloc`）。
- CMake：`anystik/CMakeLists.txt` → `myvendor/tousesub.cmake` 的 `MYVENDOR_SOURCES` / `MYVENDOR_INCLUDE_DIRS`（已指向 `${vendor_dir}/phoneloc`）。

## 文件

- `phoneloc.c`（133 行，无依赖，仅 stdint/string.h）
- `phoneloc.h`（声明 + 格式简述）