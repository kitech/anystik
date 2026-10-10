/*	$OpenBSD: sha1.c,v 1.27 2019/06/07 22:56:36 dtucker Exp $	*/

/*
 * SHA-1 in C
 * By Steve Reid <steve@edmweb.com>
 * 100% Public Domain
 *
 * Test Vectors (from FIPS PUB 180-1)
 * "abc"
 *   A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
 * "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
 *   84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
 * A million repetitions of "a"
 *   34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
 *
 * ---------------------------------------------------------------------
 * 来源：OpenSSH openssh-portable/openbsd-compat/sha1.c v1.27
 *       ($OpenBSD: sha1.c,v 1.27 2019/06/07 22:56:36 dtucker Exp $)
 *       2026-10-07 抓取。同一血统亦被 OpenBSD libc / BIND / libreSSL 使用。
 * 许可：100% Public Domain（Steve Reid 原声明；OpenBSD 后续修改同公版）。
 *
 * API：SHA1Init/SHA1Update/SHA1Final（BSD 风格，Final 参数序 = digest 在前、
 *   SHA1_CTX.count 为 u_int64_t），与现仓 qlcomp/sha1.c 的
 *   SHA1_Init/SHA1Update/SHA1Final（ctx 在前、count[2]，Steve Reid sea-to-sky
 *   变体，用户指认源自 curl）不同名——两套可同时编进同一产物而不冲突。
 *
 * 行为对比（本文件默认行为，无需任何宏）：
 *   - 本实现：SHA1Transform 无条件把输入 block memcpy 进栈 workspace 再算，
 *     默认不会覆盖传入的内存；无 static 存储，线程安全。
 *   - qlcomp/sha1.c（与 stikcommon/sha1.c 逐字节相同，diff -q 已验）：
 *     未定义 SHA1HANDSOFF 时 SHA1_Transform 把 const 输入强转后就地改写
 *     （blk0 字节序交换 + blk 消息扩展写回）——默认会覆盖传入的内存。
 *
 * 适配：删除 OpenSSH 脚手架 includes.h / WITH_OPENSSL 门控 / 4 处 DEF_WEAK；
 *   字节序改用编译器内建 __BYTE_ORDER__/__ORDER_LITTLE_ENDIAN__（gcc/clang
 *   在所有平台均预定义，Linux/macOS/Android/BSD 通用，不再依赖 glibc 专有的
 *   <endian.h>，后者在 macOS SDK 不存在）；
 *   自带 sha1_explicit_bzero：删 includes.h 后失去 OpenSSH openbsd-compat
 *   兜底，且 explicit_bzero 在 macOS 严格 C 模式（__DARWIN_C_LEVEL <
 *   __DARWIN_C_FULL）下 <string.h> 不声明，故用不可被优化掉的本地清零实现。
 * 改名：sha1.c -> sha1_ossh.c，规避与现仓 stikcommon/sha1.c、qlcomp/sha1.c
 *   的 qmake 目标 ./sha1.o 同名冲突（见 qlstik/build-qt3/Makefile:1019、
 *   build-qt6/Makefile:20686）。
 * ---------------------------------------------------------------------
 */

#include <sys/types.h>
#include <string.h>

#include "sha1_ossh.h"

/* 语义等同 explicit_bzero：volatile 写，编译器无法消除清零。
 * 不依赖任何头文件声明，Linux/macOS/Android/BSD 通用。 */
static void
sha1_explicit_bzero(void *buf, size_t len)
{
	volatile unsigned char *p = (volatile unsigned char *)buf;
	while (len--)
		*p++ = 0;
}

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/*
 * blk0() and blk() perform the initial expand.
 * I got the idea of expanding during the round function from SSLeay
 */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
# define blk0(i) (block->l[i] = (rol(block->l[i],24)&0xFF00FF00) \
    |(rol(block->l[i],8)&0x00FF00FF))
#else
# define blk0(i) block->l[i]
#endif
#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] \
    ^block->l[(i+2)&15]^block->l[i&15],1))

/*
 * (R0+R1), R2, R3, R4 are the different operations (rounds) used in SHA1
 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

typedef union {
	u_int8_t c[64];
	u_int32_t l[16];
} CHAR64LONG16;

/*
 * Hash a single 512-bit block. This is the core of the algorithm.
 */
void
SHA1Transform(u_int32_t state[5], const u_int8_t buffer[SHA1_BLOCK_LENGTH])
{
	u_int32_t a, b, c, d, e;
	u_int8_t workspace[SHA1_BLOCK_LENGTH];
	CHAR64LONG16 *block = (CHAR64LONG16 *)workspace;

	(void)memcpy(block, buffer, SHA1_BLOCK_LENGTH);

	/* Copy context->state[] to working vars */
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];

	/* 4 rounds of 20 operations each. Loop unrolled. */
	R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
	R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
	R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
	R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
	R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
	R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
	R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
	R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
	R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
	R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
	R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
	R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
	R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
	R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
	R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
	R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
	R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
	R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
	R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
	R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

	/* Add the working vars back into context.state[] */
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;

	/* Wipe variables */
	a = b = c = d = e = 0;
}


/*
 * SHA1Init - Initialize new context
 */
void
SHA1Init(SHA1_CTX *context)
{

	/* SHA1 initialization constants */
	context->count = 0;
	context->state[0] = 0x67452301;
	context->state[1] = 0xEFCDAB89;
	context->state[2] = 0x98BADCFE;
	context->state[3] = 0x10325476;
	context->state[4] = 0xC3D2E1F0;
}


/*
 * Run your data through this.
 */
void
SHA1Update(SHA1_CTX *context, const u_int8_t *data, size_t len)
{
	size_t i, j;

	j = (size_t)((context->count >> 3) & 63);
	context->count += ((u_int64_t)len << 3);
	if ((j + len) > 63) {
		(void)memcpy(&context->buffer[j], data, (i = 64-j));
		SHA1Transform(context->state, context->buffer);
		for ( ; i + 63 < len; i += 64)
			SHA1Transform(context->state, (u_int8_t *)&data[i]);
		j = 0;
	} else {
		i = 0;
	}
	(void)memcpy(&context->buffer[j], &data[i], len - i);
}


/*
 * Add padding and return the message digest.
 */
void
SHA1Pad(SHA1_CTX *context)
{
	u_int8_t finalcount[8];
	u_int i;

	for (i = 0; i < 8; i++) {
		finalcount[i] = (u_int8_t)((context->count >>
		    ((7 - (i & 7)) * 8)) & 255);	/* Endian independent */
	}
	SHA1Update(context, (u_int8_t *)"\200", 1);
	while ((context->count & 504) != 448)
		SHA1Update(context, (u_int8_t *)"\0", 1);
	SHA1Update(context, finalcount, 8); /* Should cause a SHA1Transform() */
}

void
SHA1Final(u_int8_t digest[SHA1_DIGEST_LENGTH], SHA1_CTX *context)
{
	u_int i;

	SHA1Pad(context);
	for (i = 0; i < SHA1_DIGEST_LENGTH; i++) {
		digest[i] = (u_int8_t)
		   ((context->state[i>>2] >> ((3-(i & 3)) * 8) ) & 255);
	}
	sha1_explicit_bzero(context, sizeof(*context));
}