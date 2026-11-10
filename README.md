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
  编写 `run.sh`，一条命令完成「编译 → 启动 → 压测 → 结果写入 `logs/bench.txt`」。
  用控制变量法做了四组对比：

  - 关闭日志：3019 → 3116，提升很小
  - 丢弃 `stderr` 调试输出：3116 → 3965（**+27%**），主要来源
  - 工作线程 8 → 16：3937，无变化
  - `listen` backlog 5 → 128：4068，无变化

  线程数翻倍无效，说明瓶颈在主线程：默认 Proactor 模式下，`accept`/`recv`/`writev`
  全部由主线程执行，线程池只分担 HTTP 解析。
- **修复线程池队列满时任务被静默丢弃的问题。**
  `threadpool::append()` / `append_p()` 在队列满时返回 `false`，但 `webserver.cpp` 里三处调用都没有检查返回值：
  Proactor 分支下这个连接会一直挂到定时器超时；Reactor 分支下主线程会卡在随后的 `while (true)`
  忙等里，整个服务器失去响应。
  改动：三处都改为判断返回值，失败时记录日志并调用 `deal_timer()` 关闭连接。
  验证：把队列上限临时调成 4，用 `webbench -c 100` 压测，日志中出现 47 条
  `task queue is full, close fd N`，服务器没有挂死。
- **修复 CGI 参数解析中的栈溢出。**
  `do_request()` 的 CGI 分支用 `char name[100]` 接收用户名，但复制循环没有长度检查：
  发送 300 字节的 `user=` 字段即可覆盖栈上相邻数据，实测服务器进程直接崩溃，日志停在请求体那一行。
  改动：改用 `strchr` 定位分隔符，先计算长度并校验，非法则返回 `BAD_REQUEST`，再用 `memcpy` 复制。
  顺带修正了密码偏移：原代码从 `&` 之后跳过 10 个字符，而 `&passwd=` 只有 8 个字符，
  会导致密码永远缺少前两位、登录永远失败。
  验证：同一个 payload 修复前返回空响应且进程退出，修复后返回 404 且服务正常。

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
    ./run.sh                # 一键：编译 + 启动 + 压测（结果写入 logs/bench.txt）

启动后浏览器访问 http://127.0.0.1:9006/

## 已知瓶颈与改进方向

主线程承担全部 socket I/O。改进方向：把读写也交给工作线程（真 Proactor），
或使用 `SO_REUSEPORT` 让多个线程各自 accept。

## 已知问题（读代码时发现，未修改）

- `add_status_line()` / `add_headers()` 的返回值没有被检查。当前写缓冲区 1024 字节、
  正常响应头约 60 字节，实际触发不了；但若以后增加自定义响应头需要重新评估。
- 不支持 HTTP pipelining：一次 `recv` 读到多个请求时，`init()` 会清空读缓冲区，只处理第一个。
- 读缓冲区固定 2048 字节，请求行加头部超过该长度的请求会被 `read_once()` 直接拒绝。
