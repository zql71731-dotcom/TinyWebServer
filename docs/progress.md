# TinyWebServer 学习计划 · 执行记录

> 对应 `docs/plan.md` 的阶段 A（单元 3–9）
> 执行日期：2026-10-28　｜　执行环境：VMware Ubuntu，项目路径 `~/workspace/projects/TinyWebServer`

## 一、阶段 A 结论

单元 3–9 全部完成，并顺手修掉一个继承自上游的 404 缺陷。

| 单元 | 状态 | 关键证据 |
|---|---|---|
| 3 进程与端口 | ✅ | `ss -lntp` 显示 `server pid=3` 监听 9090；`pgrep -ax server` 只匹配到自己 |
| 4 strace | ✅ | `docs/evidence/net.trace`：`accept(11)=15 → recvfrom(15) → writev(15) → close(15)` |
| 5 改端口 | ✅（有环境例外） | `config.cpp:5` 默认 9006；用 `-p 9090/9091` 验证运行时覆盖 |
| 6 加日志 | ✅ | 5 次 curl 得到 5 组 `[READ]/[WRITE]`，见 `docs/evidence/readwrite.log` |
| 7 fd 复用 | ✅ | 5 次请求的 fd 全是 **15** |
| 8 root 目录 | ✅ | `/hello.html` 返回 `{"msg":"ok"}`；缺失文件从“无响应”修成 404 |
| 9 `/hello` 路由 | ✅ | `curl /hello` 返回 `{"msg":"ok"}`，`/`、`/judge.html` 回归正常 |

证据文件都在 `docs/evidence/`：

- `net.trace`：strace 原始输出
- `readwrite.log`：READ/WRITE 与 fd
- `startup.out`、`verify.out`：启动横幅
- `http_checks.txt`：404 修复后的 HTTP 验证

## 二、逐单元记录

### 单元 3｜进程与端口

```text
LISTEN 0 5 0.0.0.0:9090 0.0.0.0:* users:(("server",pid=3,fd=11))
3 ./server -p 9090 -c 1
```

- 端口后面的 `pid=3,fd=11` 里，`3` 是**进程 PID**，`fd=11` 是监听套接字在本进程内的**文件描述符编号**
- `LISTEN` 表示套接字已完成 `bind + listen`，等待内核把新连接放进 accept 队列
- `kill <PID>` 发 SIGTERM（可被捕获，优雅退出）；`pkill -9 -f './server'` 发 SIGKILL（进程无法清理，且 `-f` 匹配整条命令行，容易误伤）
- 你之前 `pgrep -af 'server'` 匹配到一堆 `vscode-server`，就是 `-f` 的副作用。精确做法：`pgrep -ax server`（`-x` 要求进程名完全等于 `server`）
- **本机注意**：你已有的 9006 实例启动于当前 DSH 会话的 PID 命名空间之外，这里 `pgrep` 看不到它，实验中统一用 9090/9091，避免和 9006 冲突

### 单元 4｜strace 看见系统调用

实际执行：

```bash
strace -f -e trace=network,desc -o docs/evidence/net.trace ./server -p 9091 -c 1
curl -s -o /dev/null http://127.0.0.1:9091/
```

关键序列（`docs/evidence/net.trace`）：

```text
socket(AF_INET, SOCK_STREAM, ...) = 11
bind(11, ...:9091) = 0
listen(11, 5) = 0
socketpair(AF_UNIX, ...) = 0   # [13, 14]
accept(11, ...) = 15
recvfrom(15, "GET / HTTP/1.1...", 2048, ...) = 78
writev(15, [{HTTP 头 57 字节}, {HTML 586 字节}], 2) = 643
close(15) = 0
```

- 顺序：`epoll_wait` 返回 listenfd 可读 → `accept` 取连接 → `recvfrom` 读请求 → 线程池处理 → `writev` 发响应 → `close`
- **计划里的命令 `-e trace=network` 抓不到 `read/write`**：`read/write` 是通用 fd I/O，不属于 network 类；这个项目在 socket 上实际用的是 `recvfrom`（读）和 `writev`（写）。要一次看全，用 `-e trace=network,desc`，或显式 `-e trace=network,read,write,writev`
- “一个请求进来，内核先通知 epoll，再被 accept 接收”应更准确地说：**内核把新连接放进 listenfd 的 accept 队列并唤醒 `epoll_wait`，主线程再调用 `accept` 取出连接**；请求数据到达时同理，`epoll_wait` 报 `EPOLLIN`，再由 `recvfrom` 读取

### 单元 5｜运行时 `-p` 与编译期默认值

- 默认值在 `config.cpp:5`：`PORT = 9006;`
- `./server -p 9090`、`./server -p 9091` 均成功监听，证明命令行覆盖生效
- 区别：`-p` 只改本次进程的 `m_port`；改 `config.cpp` 是改默认值，必须重新 `make` 才生效
- **例外**：计划要求“9090 能访问、9006 不通”，但你的 9006 实例仍在运行（返回 200），无法制造“9006 不通”。要验证默认值失效，需要先停掉 9006 再起 9090 实例
- 附带修复：`WebServer::eventListen()` 原来在 `bind` 失败时只 `perror` 然后 `return`，`main` 仍会进入 `eventLoop()`，用未初始化的 `m_epollfd` 空转。现在改为返回 `bool`，`main` 检查失败后打印明确提示并以 1 退出：

  ```text
  bind failed: Address already in use
  ===========Server start failed===========
  端口 9095 可能已被占用，或当前用户没有绑定权限。
  换个端口重试：./server -p 9090
  ```

  验证输出见 `docs/evidence/bind_fail.out`、`bind_ok.out`

### 单元 6｜READ/WRITE 日志

代码里已有两处标记（`webserver.cpp:338`、`:385`）：

```cpp
fprintf(stderr, "[READ ] fd=%d\n", sockfd);
fprintf(stderr, "[WRITE] fd=%d\n", sockfd);
```

实验：连续 5 次 curl，输出 5 组 READ/WRITE，全部 `fd=15`（`docs/evidence/readwrite.log`）。

- 为什么用 `stderr`：不带缓冲，立刻可见；`stdout` 重定向到文件时是全缓冲，可能一行都看不到
- 为什么浏览器比 curl 多：浏览器解析 HTML 后会继续请求 `favicon.ico`、图片、CSS 等，**每个资源都是一次独立连接**，所以 READ/WRITE 成倍增加
- 三次 curl 得到三组，而不是一组，因为响应头带 `Connection: close`，每次请求后连接被关闭

### 单元 7｜fd 复用

两次连续请求都是 `fd=15`。原因：连接 `close()` 后该编号被释放，内核分配新 fd 时**优先返回最小的空闲编号**。

但要注意：**本项目的第一个 connfd 不是 5**。计划里那张“fd 从 5 开始”的表只适用于“只有 listenfd + epollfd”的最简程序。本项目在 `accept` 之前还创建了：

| fd | 是什么（`-c 1` 实测） |
|---|---|
| 0/1/2 | stdin / stdout / stderr |
| 3–10 | MySQL 连接池的 8 条连接（默认 `sql_num=8`） |
| 11 | `listenfd`（`socket → bind → listen`） |
| 12 | `epollfd`（`epoll_create`） |
| 13/14 | `socketpair`（信号管道） |
| **15** | **第一个 connfd（`accept` 返回）** |

开启日志时还会多一个日志文件 fd，所有编号整体后移；改 `-s` 连接池大小也会整体偏移。面试被问“fd 为什么是 15”时，按这个顺序解释即可。

### 单元 8｜root 目录与静态资源

```bash
printf '{"msg":"ok"}\n' > root/hello.html
curl http://127.0.0.1:9090/hello.html   # 200，内容 {"msg":"ok"}
```

- 映射规则：`URL → doc_root + URL 路径`，`doc_root` 在 `http_conn::init` 里等于项目根目录下的 `root/`
- 不存在的文件：**修复前返回“连接被直接关闭”**（curl 显示 `HTTP 000`），不是 404

  原因：`do_request()` 对 `stat` 失败返回 `NO_RESOURCE`，而 `process_write()` 的 `switch` 只处理 `INTERNAL_ERROR / BAD_REQUEST / FORBIDDEN_REQUEST / FILE_REQUEST`，`NO_RESOURCE` 落到 `default` 返回 `false`，随后 `close_conn()` 直接关连接。上游代码同样如此。

  修复：在 `http_conn.cpp` 的 `process_write()` 里把 `NO_RESOURCE` 与 `BAD_REQUEST` 合并到 404 分支。修复后：

  ```text
  HTTP/1.1 404 Not Found
  The requested file was not found on this server.
  ```

### 单元 9｜`/hello` 路由

采用计划里的**方案 A**（URL 重写到静态文件），改动在 `http_conn.cpp` 的 `do_request()`，插在 `'7'` 分支之后、默认分支之前：

```cpp
else if (strcmp(m_url, "/hello") == 0)
{
    strncpy(m_real_file + len, "/hello.html", FILENAME_LEN - len - 1);
}
```

- 为什么放这里：`do_request` 先用 `strrchr` 取 URL 最后一段做页面标志判断，`/hello` 不匹配 `0/1/5/6/7`，会落到默认分支拼成 `root/hello` 然后 `stat` 失败。新增分支在 `stat` 之前把目标改写成 `root/hello.html`
- 处理链路上的位置：`process_read → do_request → FILE_REQUEST → process_write → EPOLLOUT → write() → writev`
- 回归：`/` 200、`/judge.html` 200、`/hello.html` 200、`/hello` 200、`/nope.html` 404
- 没做方案 B（直接构造响应、不走文件）。它需要改 `process_write` 的缓冲区/iov 逻辑，适合放到单元 12 作为“自己加的功能”来讲

## 三、与计划的偏差 / 需要修正的地方

1. **启动横幅原本不存在**：计划验收要求终端输出 `===========Server is running===========`，但 `main.cpp` 没有这句。已在 `eventListen()` 后补上，并 `fflush(stdout)`，另加一行实际监听端口
2. **fd 表错误**：见单元 7，首个 connfd 是 15 而不是 5
3. **`strace -e trace=network` 抓不到 read/write**：改用 `network,desc`，实际看到的是 `recvfrom/writev`
4. **404 缺陷**：缺失文件不返回 404，而是直接断开；已修复（`http_conn.cpp`）
5. **单元 5 的“9006 不通”未验证**：你的 9006 实例还在跑，无法制造端口不可达

## 四、本阶段改动清单

| 文件 | 改动 |
|---|---|
| `main.cpp` | 启动横幅 + 端口提示 + `fflush`；`eventListen` 失败时明确报错并退出 1 |
| `webserver.cpp` / `webserver.h` | `eventListen` 改为 `bool`，`bind`/`listen`/`socket` 失败返回 false，不再用无效 epollfd 进入事件循环 |
| `http/http_conn.cpp` | 新增 `/hello → /hello.html` 映射；`NO_RESOURCE` 并入 404 分支 |
| `root/hello.html` | 新增，内容 `{"msg":"ok"}` |
| `.gitignore` | 忽略 `server`、`*.o`、`*_ServerLog`、`logs/`、`bench.txt`、`.vscode/` |
| `docs/plan.md` | 从 Windows DSH 导入的计划原件 |
| `docs/progress.md` | 本文件 |
| `docs/evidence/` | strace、READ/WRITE、HTTP 验证、bind 失败输出 |

首次提交已用仓库内临时身份 `zql <zql@localhost>`；推 GitHub 前应改成你自己的 GitHub 邮箱（`git config user.email ...`，再 `git commit --amend --reset-author`）。

## 五、阶段 B 待办（未开始）

- [ ] 安装 `webbench`（当前未安装）并压测，拿到 QPS / 平均延迟 / 失败数
- [ ] 写 `run.sh`（编译 → 启动 → 压测 → 出报告 → 清理）
- [ ] 可选对比：`-l 1` 异步日志 vs 同步日志的 QPS 差异
- [ ] 压测数字写入 README 与简历，不编造

## 六、给面试复述的三句话（阶段 A 版）

1. 请求进来是 `epoll_wait` 先返回 listenfd 可读，主线程 `accept` 得到 connfd，再把读事件丢进线程池；响应由 `writev` 把“响应头 + mmap 的静态文件”一次写出。
2. 我改过 `do_request`，把 `/hello` 重写到 `root/hello.html`，并修了缺失文件不返回 404 的问题——原来 `NO_RESOURCE` 没进 `process_write` 的分支，连接会被直接关掉。
3. fd 复用不是玄学：连接关闭后编号释放，内核下次分配优先用最小空闲编号，所以连续请求稳定拿到 15；15 之前是 8 条 MySQL 连接、listenfd、epollfd 和 socketpair。
