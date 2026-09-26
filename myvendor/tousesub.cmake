# myvendor 供应子文件：定义变量，供主项目 include 引用
# 用法：include(${CMAKE_CURRENT_SOURCE_DIR}/../myvendor/tousesub.cmake)
# 提供输出变量：
#   MYVENDOR_SOURCES        需要编入目标的 C 源文件
#   MYVENDOR_INCLUDE_DIRS   配套头文件搜索目录（murmur2 内嵌自包含，由 COMPILE_OPTIONS 传 -I）
set(myvendor_dir ${CMAKE_CURRENT_SOURCE_DIR}/../myvendor)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DBLOOM_VERSION_MAJOR=2 -DBLOOM_VERSION_MINOR=0")

set(MYVENDOR_SOURCES
    ${myvendor_dir}/netut/ipaddr_list.c
    ${myvendor_dir}/bloom/bloom.c
    ${myvendor_dir}/bloom/murmur2/MurmurHash2.c
    ${myvendor_dir}/uuid/uuid4.c
    ${myvendor_dir}/byteut/bytes2hum.c
)

set(MYVENDOR_INCLUDE_DIRS
    ${myvendor_dir}/byteut/
    ${myvendor_dir}/bloom/murmur2/
    ${myvendor_dir}/bloom/
    ${myvendor_dir}/uuid/
)
