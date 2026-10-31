#!/bin/bash
set -e
cd "$(dirname "$0")"        # 1. 切到脚本所在目录（可选，但推荐）
make clean && make          # 2. 编译
./server -l 1 2>/dev/null & # 3. 后台起服务器
PID=$!                      # 4. 立刻记下进程号
trap 'kill $PID 2>/dev/null' EXIT   # 5. 立刻装警报
sleep 1                     # 6. 等它就绪
./test_pressure/webbench-1.5/webbench -c 100 -t 10 http://127.0.0.1:9006/ | tee bench.txt   # 7. 压测