#!/bin/sh
# 在本机编译并运行单元测试。
#
# 只编译 rfid_core 这个库和测试文件，不碰任何单片机相关的代码。
# 所以不用插板子，也不用装交叉编译工具链，一条命令几秒钟出结果。
#
# 用法：sh scripts/run_host_tests.sh

set -eu

# 从脚本所在位置往上一级，得到工程根目录。
# 这样不管在哪个目录下调用这个脚本都能跑对。
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/.cache/host-tests"

mkdir -p "$build_dir"

# 编译选项分三组，作用各不相同：
#
# 警告组 -Wall 到 -Wconversion
#   -Werror 把警告直接变成错误，逼着代码写干净。
#   -Wconversion 会揪出隐式的类型转换，嵌入式代码里这类问题最容易埋坑。
#
# 消毒组 -fsanitize=address,undefined
#   address    抓数组越界和野指针。
#   undefined  抓整数溢出、错位移位这类未定义行为。
#   这两个在电脑上几乎零成本，在单片机上根本没法用，所以本机测试很值。
#
# -fno-omit-frame-pointer 保留栈帧，出错时能打出可读的调用栈。
clang \
  -std=c11 \
  -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  -I"$project_dir/lib/rfid_core/include" \
  -I"$project_dir/include" \
  "$project_dir/test/native/test_main.c" \
  "$project_dir/lib/rfid_core/src/rfid_protocol.c" \
  "$project_dir/lib/rfid_core/src/rfid_reader.c" \
  "$project_dir/lib/rfid_core/src/tts_service.c" \
  "$project_dir/src/soft_start.c" \
  "$project_dir/src/announce.c" \
  -o "$build_dir/native_tests"

# 跑测试。有断言失败就返回非 0，配合上面的 set -e 让整个脚本失败。
"$build_dir/native_tests"
