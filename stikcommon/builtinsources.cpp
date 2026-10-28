#include "builtinsources.h"

// ── 内置下载源唯一表（自 anystik/src/stickerstore.cpp 原样迁入）────────────
// 逐字节搬运 anystik/src/stickerstore.cpp:3353-3669 的 60 条初始化项，
// 唯一差异：B站表情全归档的 4365762560L 改为 4365762560LL（防 32 位 long 溢出）。
const BuiltinSource kBuiltinSources[] = {
    { "DeepSeek酱 鲸鱼娘 梗图 preview(19)",
      "https://gh-proxy.org/https://github.com/the-beating-light-of-the-nail/deepseek-chan-meme-pack/archive/3678bd997602446aedb9b2854ebee7400c808138.zip",
      715185L,    // 2026-09-15 实测 GET 全量字节（仓库 archive，含 19 张 preview webp + README）
      "https://github.com/the-beating-light-of-the-nail/deepseek-chan-meme-pack",
      "2026-09-15", true },
    { "奶龙 抽象梗图",
      "https://gh-proxy.org/https://github.com/GGGeeeooorrrgggeee/nailong-memes/archive/ada8a505e1bfcd40e0c0ad9962b45d20ffda0599.zip",
      153480410L,  // 2026-09-16 实测 GET 全量字节：141 文件 / 136 张 gif+jpg（均在 gif/ ），sha 钉死
      "https://github.com/GGGeeeooorrrgggeee/nailong-memes",
      "2026-09-16", true },
    { "中国社交媒体平台表情合集 (emoji-chinese)",
      "https://gh-proxy.org/https://github.com/YiJio/emoji-chinese/archive/79c2292b778cf93cb5850f70c3168bcbfe39aeeb.zip",
      16108164L,   // 2026-09-16 API 树求和（≈15.4MB，1280 png + 698 gif，QQ/微信/抖音/B站/微博等多平台默认表情）
      "https://github.com/YiJio/emoji-chinese",
      "2026-09-16", true },
    { "EmojiPackage 中文斗图配文合集",
      "https://gh-proxy.org/https://github.com/getActivity/EmojiPackage/archive/6110519a8340b36ae3497c4b062292ca25291b61.zip",
      202812407L,   // 2026-09-16 API 树求和（≈193.4MB，302 gif / 1537 jpg / 129 png / 15 webp）
      "https://github.com/getActivity/EmojiPackage",
      "2026-09-16", true },
    { "smiles 聊天动态表情 (AC娘等)",
      "https://gh-proxy.org/https://github.com/LiuJi-Jim/smiles/archive/e710ecc1d22e012231175fb88dbc60291de4ce83.zip",
      30089460L,    // 2026-09-16 API 树求和（≈28.7MB，219 gif / 137 jpg / 13 png）
      "https://github.com/LiuJi-Jim/smiles",
      "2026-09-16", true },
    { "QQ 官方表情全系 (QFace)",
      "https://gh-proxy.org/https://github.com/koishijs/QFace/archive/f835d447fb5eb4aa9ae733f31d90cdd6d5589093.zip",
      143277322L,  // 2026-09-16 API 树求和（≈136.6MB，94 表情 / 949 资源，含 gif/webp/png）
      "https://koishi.js.org/QFace/",
      "2026-09-16", true },
    { "QQ 超级表情 (37张)",
      "https://gh-proxy.org/https://github.com/Iamliuxiaozhen/QQemoji/archive/571be5c0f9c304dd2c34511578aa7233637e0c4d.zip",
      7164746L,   // 2026-09-16 API 树求和（≈6.8MB，super/ 37 张 png）
      "https://github.com/Iamliuxiaozhen/QQemoji",
      "2026-09-16", true },
    { "4chan 表情包 (2022)",
      "https://gh-proxy.org/https://github.com/Sundowner8/4chanmotes.github.io/archive/6ac1b8072cff9a0f92c3e86a236c234953c324c8.zip",
      590793L,    // 2026-09-16 API 树求和（≈0.6MB，emotes/ 约 175 张 png/gif）
      "https://github.com/Sundowner8/4chanmotes.github.io",
      "2026-09-16", true },
    { "blobs.gg 梗图 GIF (英文)",
      "https://gh-proxy.org/https://github.com/Raymo111/emoji/archive/ec4fa3b7d74dfd299570cb63ab6ce9d74e4b6f01.zip",
      24625769L,  // 2026-09-16 API 树求和（≈23.5MB，600+ 张 gif/webp）
      "https://github.com/Raymo111/emoji",
      "2026-09-16", true },
    { "Discord/Slack 自制表情 (skullface)",
      "https://gh-proxy.org/https://github.com/skullface/emotes/archive/36915096b337f98e9e7581f95fec46ed2b39f807.zip",
      4271880L,   // 2026-09-16 API 树求和（≈4.1MB，emoji/ 114 张 png/gif）
      "https://github.com/skullface/emotes",
      "2026-09-16", true },
    { "WhatsApp 官方示例贴纸 (SDK)",
      "https://gh-proxy.org/https://github.com/WhatsApp/stickers/archive/06144a1f6077bbb346e1230032fc4e0bce996d03.zip",
      13163057L,   // 8/30 selftest5 整包实测（约值，随 commit 变化）
      "https://github.com/WhatsApp/stickers",
      "2025-11-07", true },
    { "Animals (Telegram)",
      "https://gh-proxy.org/https://raw.githubusercontent.com/kanelai/stickerapp/b84a74ab09db90e33bd6eaee7135bf96a00fb5dd/Animals.stickerpack",
      1088205L,    // 本会话 Range 206 实测 content-range: bytes 0-0/1088205
      "https://github.com/kanelai/stickerapp",
      "2019-02-18", true },
    { "LINE 贴纸包 (GitHub 镜像)",
      "https://gh-proxy.org/https://raw.githubusercontent.com/porridgebrother/line-stickers/8480a02a3b74914d70b8763cef77c2a9a86769a5/stickers.zip",
      1490697L,   // 本会话 Range 实测 content-range: bytes 0-0/1490697（raw 偶发超时,重试即可）
      "https://github.com/porridgebrother/line-stickers",
      "2017-09-18", true },
    { "wyv 表情包 (Volpeon)",
      "https://strapi.volpeon.ink/uploads/wyv_10ef1cb106.zip",
      680387L,    // 2026-09-19 HEAD 实测 content-length（≈0.68 MB，31 张 png 根目录直达，含 LICENSE/meta.json）
      "https://volpeon.ink/emojis/wyv/",
      "2026-09-19", true },
    { "ChineseBQB 梗图包",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/001Funny_%E6%BB%91%E7%A8%BD%E5%A4%A7%E4%BD%AC%F0%9F%98%8FBQB.zip",
      4691509L,
      "https://v2fy.com/p/001Funny_%E6%BB%91%E7%A8%BD%E5%A4%A7%E4%BD%AC%F0%9F%98%8FBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },   // 本会话 Range 实测 content-range: bytes 0-0/4691509（raw 源偶发超时,重试即可）
    { "LINE 贴纸 2938",
      "https://stickershop.line-scdn.net/stickershop/v1/product/2938/iphone/stickers@2x.zip",
      797156L,         // 真机 HEAD 实测 content-length（≈0.76 MB）
      "https://store.line.me/stickershop/product/2938/zh-Hans",
      "2026-09-19", true },
    { "LINE 动态 18060",
      "https://stickershop.line-scdn.net/stickershop/v1/product/18060/iphone/stickerpack@2x.zip",
      7246424L,         // 真机 HEAD 实测 content-length（≈6.9 MB）
"https://store.line.me/stickershop/product/18060/zh-Hans",
      "2026-09-19", true },
     { "小红书表情包（社交平台合集）",
       "https://gh-proxy.org/https://github.com/Augenstern-O/Stickers/archive/92767d783cc80d3e729217b5897752035da75fc9.zip",
       320394593L,          // 本会话实测下载（≈320.0 MB）
       "https://github.com/Augenstern-O/Stickers",
      "2026-08-01", true },
     { "Twitter 官方 Emoji (Twemoji)",
       "https://gh-proxy.org/https://github.com/twitter/twemoji/archive/bad3bceeafc901ace42a3dfe0421db6388daafb9.zip",
       33554432L,         // 指定 32M
       "https://github.com/twitter/twemoji",
      "2026-07-07", true },
     { "贴吧表情全收集（滑稽等）",
       "https://gh-proxy.org/https://github.com/KeikoAyano/Tieba-Emoji/archive/aabbbbfaa2ccd6540cb5f13bc09b0cd1c64089c5.zip",
       17447147L,         // API 求和（≈16.6 MB）
       "https://github.com/KeikoAyano/Tieba-Emoji",
      "2025-11-02", true },
     { "抖音表情包（默认/合成全收集）",
       "https://gh-proxy.org/https://github.com/rento666/douyin-emoji/archive/46f5cba582a70206d79f492908cf8509d8784398.zip",
       35398057L,         // API 求和（≈33.8 MB）
       "https://github.com/rento666/douyin-emoji",
      "2025-11-27", true },
{ "B站表情全归档（ccmuyuu）",
       "https://gh-proxy.org/https://github.com/ccmuyuu/bilibili-emotes/archive/db3972317ee029f5acf28ba7bbe971cbbc4fd4e9.zip",
       4365762560LL,       // 首次预估=API size 4263440KB（≈4.07 GB）；LL 防 32 位 long 溢出
       "https://github.com/ccmuyuu/bilibili-emotes",
      "2026-09-06", true },
     { "B站贴纸存档（amtoaer）",
       "https://gh-proxy.org/https://github.com/amtoaer/bilibili-stickers/archive/abab458b399659f4ae6d91f3633c3a8365b147ac.zip",
       1686083L,          // API 求和（≈1.6 MB）
       "https://github.com/amtoaer/bilibili-stickers",
      "2023-05-21", true },
{ "B站表情gif全图（rtransformation）",
        "https://gh-proxy.org/https://github.com/rtransformation/Bilibili-emoticon-collection/archive/a2f0c55a04f689a23382a594791957d8b01aff70.zip",
        3546883L,          // API 求和（≈3.4 MB）
        "https://github.com/rtransformation/Bilibili-emoticon-collection",
      "2022-01-25", true },
     { "brd 表情包 (Volpeon)",
        "https://strapi.volpeon.ink/uploads/brd_560ac976ea.zip",
        692745L,          // 2026-09-19 HEAD 实测 content-length（≈0.68 MB）
        "https://volpeon.ink/emojis/brd/",
      "2026-09-19", true },
     { "wlf 表情包 (Volpeon)",
        "https://strapi.volpeon.ink/uploads/wlf_6b2776ad62.zip",
        841722L,          // 2026-09-19 HEAD 实测 content-length（≈0.84 MB）
        "https://volpeon.ink/emojis/wlf/",
      "2026-09-19", true },
     { "Clannad 团子表情 (NaiJi/udongein)",
        "https://udongein.xyz/emoji-page/dangos/dangos.zip",
        147315L,          // 2026-09-19 HEAD 实测 content-length
        "https://udongein.xyz/emoji-page/",
      "2026-09-19", true },
     { "雀魂表情 (NaiJi/udongein)",
        "https://udongein.xyz/emoji-page/mahjong-soul/mahjong-soul.zip",
        2487739L,         // 2026-09-19 HEAD 实测 content-length（≈2.5 MB）
        "https://udongein.xyz/emoji-page/",
      "2026-09-19", true },
     { "moule 角色包 HYPERHYENA",
        "https://moule.world/media/images/emojis/HYPERHYENA.zip",
        1024677L,         // 2026-09-19 HEAD 实测 content-length（≈1.0 MB）
        "https://moule.world/",
      "2026-09-19", true },
     { "moule 角色包 PWNZR",
        "https://moule.world/media/images/emojis/PWNZR.zip",
        719654L,          // 2026-09-19 HEAD 实测 content-length（≈0.72 MB）
        "https://moule.world/",
      "2026-09-19", true },
     { "mutant 表情 (fedi-emojis)",
        "https://gh-proxy.org/https://raw.githubusercontent.com/SnenxyTengoku/fedi-emojis/main/mutant.zip",
        21171683L,        // 2026-09-19 HEAD 实测 content-length（≈21 MB）
        "https://github.com/SnenxyTengoku/fedi-emojis",
      "2026-09-19", true },
     { "mutantremix (fedi-emojis)",
        "https://gh-proxy.org/https://raw.githubusercontent.com/SnenxyTengoku/fedi-emojis/main/mutantremix.zip",
        255372L,          // 2026-09-19 HEAD 实测 content-length（≈0.25 MB）
        "https://github.com/SnenxyTengoku/fedi-emojis",
      "2026-09-19", true },
     { "blobbee (olivvybee)",
        "https://github.com/olivvybee/emojis/releases/download/2026.08.16.1/blobbee.zip",
        1836542L,         // 2026-09-19 HEAD 实测 content-length（≈1.8 MB）
        "https://github.com/olivvybee/emojis",
      "2026-09-19", true },
     { "neobread (olivvybee)",
        "https://github.com/olivvybee/emojis/releases/download/2026.08.16.1/neobread.zip",
        1298158L,         // 2026-09-19 HEAD 实测 content-length（≈1.3 MB）
        "https://github.com/olivvybee/emojis",
      "2026-09-19", true },
      // ── ChineseBQB 精选包（来源 zhaoolee/ChineseBQB 仓库 README 直链；本会话并行 HEAD 实测 content-length）──
    { "ChineseBQB 002 可爱的女孩纸👧",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/002CuteGirl_%E5%8F%AF%E7%88%B1%E7%9A%84%E5%A5%B3%E5%AD%A9%E7%BA%B8%F0%9F%91%A7BQB.zip",
      59028547L,
      "https://v2fy.com/p/002CuteGirl_%E5%8F%AF%E7%88%B1%E7%9A%84%E5%A5%B3%E5%AD%A9%E7%BA%B8%F0%9F%91%A7BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 003 可爱男孩纸👶",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/003CuteBoy_%E5%8F%AF%E7%88%B1%E7%94%B7%E5%AD%A9%E7%BA%B8%F0%9F%91%B6BQB.zip",
      10535808L,
      "https://v2fy.com/p/003CuteBoy_%E5%8F%AF%E7%88%B1%E7%94%B7%E5%AD%A9%E7%BA%B8%F0%9F%91%B6BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 006 仓鼠🐹",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/006Hamster_%E4%BB%93%E9%BC%A0%F0%9F%90%B9BQB.zip",
      1984744L,
      "https://v2fy.com/p/006Hamster_%E4%BB%93%E9%BC%A0%F0%9F%90%B9BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 007 胖虎🐯",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/007Tiger_%E8%83%96%E8%99%8E%F0%9F%90%AFBQB.zip",
      443084L,
      "https://v2fy.com/p/007Tiger_%E8%83%96%E8%99%8E%F0%9F%90%AFBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 009 熊本熊🐻",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/009KumamotoBear_%E7%86%8A%E6%9C%AC%E7%86%8A%F0%9F%90%BBBQB.zip",
      4822035L,
      "https://v2fy.com/p/009KumamotoBear_%E7%86%8A%E6%9C%AC%E7%86%8A%F0%9F%90%BBBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 015 金馆长熊猫🐼",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/015Golden_Curator_Panda%E9%87%91%E9%A6%86%E9%95%BF%E7%86%8A%E7%8C%AB%F0%9F%90%BCBQB.zip",
      4789453L,
      "https://v2fy.com/p/015Golden_Curator_Panda%E9%87%91%E9%A6%86%E9%95%BF%E7%86%8A%E7%8C%AB%F0%9F%90%BCBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-10-28", true },
    { "ChineseBQB 010 是喵星人啦🐱",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/010Cat_%E6%98%AF%E5%96%B5%E6%98%9F%E4%BA%BA%E5%95%A6%F0%9F%90%B1BQB.zip",
      24245100L,
      "https://v2fy.com/p/010Cat_%E6%98%AF%E5%96%B5%E6%98%9F%E4%BA%BA%E5%95%A6%F0%9F%90%B1BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 011 狗🐶",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/011Dog_%E7%8B%97%F0%9F%90%B6BQB.zip",
      19551423L,
      "https://v2fy.com/p/011Dog_%E7%8B%97%F0%9F%90%B6BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 013 小猪佩奇👑",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/013PigPecs_%E5%B0%8F%E7%8C%AA%E4%BD%A9%E5%A5%87%F0%9F%91%91BQB.zip",
      3866090L,
      "https://v2fy.com/p/013PigPecs_%E5%B0%8F%E7%8C%AA%E4%BD%A9%E5%A5%87%F0%9F%91%91BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 026 小黄鸡🐔",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/026Chicken_%E5%B0%8F%E5%B9%BA%E9%B8%A1%F0%9F%90%94BQB.zip",
      943786L,
      "https://v2fy.com/p/026Chicken_%E5%B0%8F%E5%B9%BA%E9%B8%A1%F0%9F%90%94BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 031 沙雕企鹅🐧",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/031Penguin_%E6%B2%99%E9%9B%95%E4%BC%81%E9%B9%85%F0%9F%90%A7BQB.zip",
      1645177L,
      "https://v2fy.com/p/031Penguin_%E6%B2%99%E9%9B%95%E4%BC%81%E9%B9%85%F0%9F%90%A7BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 035 猫和老鼠",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/035TomAndJerry_%E7%8C%AB%E5%92%8C%E8%80%81%E9%BC%A0BQB.zip",
      40770934L,
      "https://v2fy.com/p/035TomAndJerry_%E7%8C%AB%E5%92%8C%E8%80%81%E9%BC%A0BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 036 皮卡丘",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/036Pikachu_%E7%9A%AE%E5%8D%A1%E4%B8%98BQB.zip",
      31541L,
      "https://v2fy.com/p/036Pikachu_%E7%9A%AE%E5%8D%A1%E4%B8%98BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", false },
    { "ChineseBQB 039 姚明（三巨头）",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/039YaoMing%E8%A1%A8%E6%83%85%E5%8C%85%E4%B8%89%E5%B7%A8%E5%A4%B4_%E5%A7%9A%E6%98%8EBQB.zip",
      70017L,
      "https://v2fy.com/p/039YaoMing%E8%A1%A8%E6%83%85%E5%8C%85%E4%B8%89%E5%B7%A8%E5%A4%B4_%E5%A7%9A%E6%98%8EBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", false },
    { "ChineseBQB 040 花泽香菜（三巨头）",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/040HanazawaKana%E8%A1%A8%E6%83%85%E5%8C%85%E4%B8%89%E5%B7%A8%E5%A4%B4_%E8%8A%B1%E6%B3%BD%E9%A6%99%E8%8F%9CBQB.zip",
      24276L,
      "https://v2fy.com/p/040HanazawaKana%E8%A1%A8%E6%83%85%E5%8C%85%E4%B8%89%E5%B7%A8%E5%A4%B4_%E8%8A%B1%E6%B3%BD%E9%A6%99%E8%8F%9CBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", false },
    { "ChineseBQB 048 海绵宝宝",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/048SpongeBob_%E6%B5%B7%E7%BB%B5%E5%AE%9D%E5%AE%9DBQB.zip",
      217974L,
      "https://v2fy.com/p/048SpongeBob_%E6%B5%B7%E7%BB%B5%E5%AE%9D%E5%AE%9DBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 052 杰尼龟",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/052Squirtle_%E6%9D%B0%E5%B0%BC%E9%BE%9FBQB.zip",
      2775654L,
      "https://v2fy.com/p/052Squirtle_%E6%9D%B0%E5%B0%BC%E9%BE%9FBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 056 哆啦A梦",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/056Doraemon_%E5%93%86%E5%95%A6A%E6%A2%A6BQB.zip",
      6476953L,
      "https://v2fy.com/p/056Doraemon_%E5%93%86%E5%95%A6A%E6%A2%A6BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 060 Mur猫😺",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/060MurCat_Mur%E7%8C%AB%F0%9F%98%BABQB.zip",
      2761270L,
      "https://v2fy.com/p/060MurCat_Mur%E7%8C%AB%F0%9F%98%BABQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 062 蔡徐坤🏀",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/062CaiXvKun_%E8%94%A1%E5%BE%90%E5%9D%A4%F0%9F%8F%80BQB.zip",
      3759098L,
      "https://v2fy.com/p/062CaiXvKun_%E8%94%A1%E5%BE%90%E5%9D%A4%F0%9F%8F%80BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 064 特朗普",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/064Trump_%E7%89%B9%E6%9C%97%E6%99%AEBQB.zip",
      561200L,
      "https://v2fy.com/p/064Trump_%E7%89%B9%E6%9C%97%E6%99%AEBQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", false },
    { "ChineseBQB 065 旅行青蛙🐸",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/065TravelFrog_%E6%97%85%E8%A1%8C%E9%9D%92%E8%9B%99%F0%9F%90%B8BQB.zip",
      320710L,
      "https://v2fy.com/p/065TravelFrog_%E6%97%85%E8%A1%8C%E9%9D%92%E8%9B%99%F0%9F%90%B8BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 070 JOJO的奇妙冒险",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/070JOJO%E7%9A%84%E5%A5%87%E5%A6%99%E5%86%92%E9%99%A9BQB.zip",
      223508L,
      "https://v2fy.com/p/070JOJO%E7%9A%84%E5%A5%87%E5%A6%99%E5%86%92%E9%99%A9BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 089 饮茶哥",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/089TeaBoy_%E9%A5%AE%E8%8C%B6%E5%93%A5_BQB.zip",
      1774698L,
      "https://v2fy.com/p/089TeaBoy_%E9%A5%AE%E8%8C%B6%E5%93%A5_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 095 原神",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/095GenShin_%E5%8E%9F%E7%A5%9E_BQB.zip",
      16702253L,
      "https://v2fy.com/p/095GenShin_%E5%8E%9F%E7%A5%9E_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 101 电锯人",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/101ChainsawMan_%E7%94%B5%E9%94%AF%E4%BA%BA_BQB.zip",
      846057L,
      "https://v2fy.com/p/101ChainsawMan_%E7%94%B5%E9%94%AF%E4%BA%BA_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 105 黑神话悟空🐒",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/105_BlackMythWuKong_%E9%BB%91%E7%A5%9E%E8%AF%9D%E6%82%9F%E7%A9%BA%F0%9F%90%92_BQB.zip",
      1180957L,
      "https://v2fy.com/p/105_BlackMythWuKong_%E9%BB%91%E7%A5%9E%E8%AF%9D%E6%82%9F%E7%A9%BA%F0%9F%90%92_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
    { "ChineseBQB 106 芙莉莲🪄",
      "https://gh-proxy.org/https://raw.githubusercontent.com/zhaoolee/ChineseBQB/ffd0e2e826e48aac67e8e6e15807e934a77216f6/106_Frieren_%E8%8A%99%E8%8E%89%E8%8E%B2%F0%9F%AA%84_BQB.zip",
      6236255L,
      "https://v2fy.com/p/106_Frieren_%E8%8A%99%E8%8E%89%E8%8E%B2%F0%9F%AA%84_BQB/?post_category=%E4%B8%AD%E5%9B%BD%E4%BA%BA%E7%9A%84%E8%A1%A8%E6%83%85%E5%8C%85-pp%E5%88%B6%E9%80%A0%E8%AE%A1%E5%88%92-chinesebqb",
      "2026-06-19", true },
{ "新飞飞QQ表情包 (eif)",
       "https://cz.197942.com/dl/lx/xinff.zip",
       167841L,    // 2013-01-04 cr173 页面更新日；zip 内含 新飞飞表情包.eif（1602050B，74 张）
       "https://www.cr173.com/soft/53605.html",
       "2013-01-04", true },
     { "Telegram 官方动图贴纸样例 (TGS)",
       "https://codeload.github.com/kuronekowen/Telegram-Sticker-Sample/zip/"
       "334150dbf20b010dbb6393baccc1f596f046a1f3",
       778380L,       // 2026-09-25 实测下载字节；zip 内含 6 张 tgs + 6 份 Lottie JSON
       "https://github.com/kuronekowen/Telegram-Sticker-Sample",
       "2021-11-13", true },
};
const unsigned kBuiltinSourceCount =
    sizeof(kBuiltinSources) / sizeof(kBuiltinSources[0]);

namespace {

const char* const kGhProxyPrefixes[] = {
    "https://gh-proxy.com/",
    "https://gh-proxy.org/"
};
const int kGhProxyPrefixCount = 2;

bool tableUrlHit(const QString& url)
{
    if (url.isEmpty())
        return false;
    const QString urlNorm = builtinsrc::normalizeUrl(url);
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i) {
        if (urlNorm == builtinsrc::normalizeUrl(
                            QString::fromUtf8(kBuiltinSources[i].url)))
            return true;
    }
    return false;
}

bool tableNameHit(const QString& title)
{
    for (unsigned i = 0; i < kBuiltinSourceCount; ++i) {
        const QString name = QString::fromUtf8(kBuiltinSources[i].name);
        if (title == name || title == builtinsrc::sanitizeDirName(name))
            return true;
    }
    return false;
}

} // namespace

QString builtinsrc::normalizeUrl(const QString& url)
{
    for (int i = 0; i < kGhProxyPrefixCount; ++i) {
        const QString prefix = QString::fromLatin1(kGhProxyPrefixes[i]);
        if (url.startsWith(prefix))
            return url.mid(prefix.length());
    }
    return url;
}

QString builtinsrc::sanitizeDirName(const QString& name)
{
    const QString illegal = QString::fromLatin1("/\\:*?\"<>|");
    QString out;
    // 索引循环而非 range-for：Qt3 的 QString 无 begin()/end()
    for (int ci = 0; ci < name.length(); ++ci) {
        const QChar c = name.at(ci);
        if (!c.isPrint())
            continue;
        if (illegal.contains(c))
            continue;
        if (c.unicode() < 0x20)     // 原 QLatin1Char(' ')：Qt3 无 QLatin1Char
            continue;
        out.append(c);
    }
    if (out.isEmpty())
        out = QString::fromLatin1("pack");
    return out;
}

bool builtinsrc::matches(const QString& url, const QString& title)
{
    return tableUrlHit(url) || tableNameHit(title);
}

QString builtinsrc::diag(const QString& packId, const QString& url,
                         const QString& title)
{
    const QString urlNorm = builtinsrc::normalizeUrl(url);
    const bool urlHit = tableUrlHit(url);
    const bool titleHit = tableNameHit(title);
    return QString::fromLatin1(
               "id=%1 title=%2 meta.url=%3 urlNorm=%4 urlHit=%5 builtinTitleHit=%6")
        .arg(packId)
        .arg(title)
        .arg(url.isEmpty() ? QString::fromLatin1("(empty)") : url)
        .arg(urlNorm.isEmpty() ? QString::fromLatin1("(empty)") : urlNorm)
        .arg(urlHit ? QString::fromLatin1("YES") : QString::fromLatin1("NO"))
        .arg(titleHit ? QString::fromLatin1("YES") : QString::fromLatin1("NO"));
}
