#!/bin/bash
# 拉取 CI 构建依赖的外部仓库到 workspace 父级（$GITHUB_WORKSPACE/..）。
#
# 仓库内以下目录为提交成符号链接的实体，fresh checkout 时悬空，须先克隆到位：
#   qlcomp/   → ../qxcomp/qlcomp
#   myvendor/ → ../../touse/{bloom,byteut,netut,uuid}
# qlappcom/ 与 vendor/ 已作为普通文件入库，不在此列。
set -eux

git clone https://github.com/qxcomp/qxcomp.git ../qxcomp

# git clone https://github.com/qxcomp/cxvendor.git ../cxvendor
# git clone https://github.com/qxcomp/droidcomp.git ../droidcomp

git clone https://github.com/kitech/touse.git ../touse