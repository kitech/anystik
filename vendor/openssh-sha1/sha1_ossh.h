/*	$OpenBSD: sha1.h,v 1.24 2012/12/05 23:19:57 deraadt Exp $	*/

/*
 * SHA-1 in C
 * By Steve Reid <steve@edmweb.com>
 * 100% Public Domain
 *
 * ---------------------------------------------------------------------
 * 改名：sha1.h -> sha1_ossh.h
 *   规避与现仓 stikcommon/sha1.h（__SHA1_H）及 qlcomp/sha1.h 同名。
 *   头守卫 __SHA1_OSSH_H 亦不同，两套 SHA1 均可 include 到同一 TU 而不冲突
 *   （注意：同一 TU 内 struct SHA1_CTX 定义一个即可，勿两套同时用）。
 * 裁剪：仅保留本 .c 实际实现的 SHA1Init/SHA1Pad/SHA1Transform/SHA1Update/
 *   SHA1Final 声明；删除上游 SHA1End/File/FileChunk/Data 声明、__bounded__
 *   属性、HTONDIGEST/NTOHDIGEST（无 htonl 依赖）。
 * 类型：u_int* 来自 <sys/types.h>（glibc 提供）。
 * ---------------------------------------------------------------------
 */

#ifndef __SHA1_OSSH_H
#define __SHA1_OSSH_H

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	SHA1_BLOCK_LENGTH		64
#define	SHA1_DIGEST_LENGTH		20

typedef struct {
    u_int32_t state[5];
    u_int64_t count;
    u_int8_t buffer[SHA1_BLOCK_LENGTH];
} SHA1_CTX;

void SHA1Init(SHA1_CTX *);
void SHA1Pad(SHA1_CTX *);
void SHA1Transform(u_int32_t [5], const u_int8_t [SHA1_BLOCK_LENGTH]);
void SHA1Update(SHA1_CTX *, const u_int8_t *, size_t);
void SHA1Final(u_int8_t [SHA1_DIGEST_LENGTH], SHA1_CTX *);

#ifdef __cplusplus
}
#endif

#endif /* __SHA1_OSSH_H */