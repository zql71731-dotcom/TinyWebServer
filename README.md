# TinyWebServer（C++ 轻量级 Web 服务器 · 学习与改造）

基于 [qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer) 的学习与改造。
原项目是一个使用 epoll + 线程池 + 定时器的 C++ Web 服务器。

## 我做了什么

 - **新增 `/hello` 路由，改为由代码直接构造响应。**
  原项目把 URL 改写成 `hello.html`，再用 `open` 打开文件、`mmap` 映射到内存，返回一个 HTML 页面。
  改为在内存中直接拼出 `{"msg":"ok"}`，加上 HTTP 头返回，不再读取磁盘上的文件。
  验证：`strace -f -e trace=openat` 记录系统调用，其中 `hello.html` 出现 **0** 次，
  作为对照，`judge.html` 出现 **2** 次（`/` 走的是读文件路径）。
- **日志输出改到 `logs/` 目录。**
  原先日志写在项目根目录下（如 `2026_10_30_ServerLog`），压测时 10 秒可写 145MB，文件大量堆在仓库根目录；
  且 `.gitignore` 中的 `*_ServerLog` 匹配不到日志切分产生的 `.1` 文件，导致它一直出现在 `git status` 的未跟踪列表里。
  改动：启动时 `mkdir("./logs")`，日志路径改为 `./logs/ServerLog`，`.gitignore` 改为 `*_ServerLog*`。
  结果：日志集中在 `logs/` 目录下，仓库根目录保持干净。
- **压测与瓶颈定位。**
  编写 `run.sh`，一条命令完成「编译 → 启动 → 压测 → 结果写入 `bench.txt`」。
  用控制变量法做了四组对比：

  - 关闭日志：3019 → 3116，提升很小
  - 丢弃 `stderr` 调试输出：3116 → 3965（**+27%**），主要来源
  - 工作线程 8 → 16：3937，无变化
  - `listen` backlog 5 → 128：4068，无变化

  线程数翻倍无效，说明瓶颈在主线程：默认 Proactor 模式下，`accept`/`recv`/`writev`
  全部由主线程执行，线程池只分担 HTTP 解析。

## 压测数据

环境：VMware Ubuntu（16 vCPU），`webbench -c 100 -t 10`，目标 `/`。

| 配置 | QPS |
|---|---|
| 日志开 | 3019 |
| 日志关 | 3116 |
| 日志关 + 丢弃 stderr | 3965 |
| 线程 8 → 16 | 3937（无变化） |
| listen backlog 5 → 128 | 4068（无变化） |

**结论**：瓶颈在主线程。默认 `actor_model=0`（Proactor）下，
`accept` / `recv` / `writev` 全部由主线程执行，线程池只分担 HTTP 解析，
所以增加工作线程对 QPS 没有影响。

## 编译与运行

    make                    # 编译
    ./server                # 启动，默认端口 9006
    ./server -p 9090        # 指定端口启动
    ./run.sh                # 一键：编译 + 启动 + 压测（结果写入 bench.txt）

启动后浏览器访问 http://127.0.0.1:9006/

## 已知瓶颈与改进方向

主线程承担全部 socket I/O。改进方向：把读写也交给工作线程（真 Proactor），
或使用 `SO_REUSEPORT` 让多个线程各自 accept。