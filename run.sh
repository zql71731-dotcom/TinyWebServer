#!/bin/bash
set -e
cd "$(dirname "$0")"        # 1. 切到脚本所在目录（可选，但推荐）
mkdir -p logs               # 2. 保证 logs/ 存在（日志和压测结果都写这里）
make clean && make          # 3. 编译
./server -l 1 2>/dev/null & # 4. 后台起服务器
PID=$!                      # 5. 立刻记下进程号
trap 'kill $PID 2>/dev/null' EXIT   # 6. 立刻装警报
sleep 1                     # 7. 等它就绪
./test_pressure/webbench-1.5/webbench -c 100 -t 10 http://127.0.0.1:9006/ | tee logs/bench.txt   # 8. 压测
