# TinyWebServer 学习与改造计划

> 用途：把 TinyWebServer 从「能跑通」变成「我改过、我压过、我讲得清」的项目，作为零经验求职的核心证据。
> 整理日期：2026-10-28　｜　对应项目路径：`~/workspace/projects/TinyWebServer`（VMware 虚拟机内）

---

## 一、总目标与截止时间

### 总目标

产出三样**可验证**的东西，支撑简历与面试：

1. **一个能打开的 GitHub 仓库**（真实代码 + README + 压测数据）
2. **一段能讲 10 分钟不卡壳的改造经历**（改了什么、为什么改、效果如何）
3. **项目八问全部能答**（见第七节）

**判断标准**：不是「我学完了」，而是「面试官随便问，我答得出来」。

### 截止时间

| 里程碑 | 截止日 | 到期必须有的东西 |
|---|---|---|
| 阶段 A：能跑 + 能改 | 2026-10-29 | 跑通、改过端口、加过日志、加过 `/hello` 路由 |
| 阶段 B：能压 + 有数字 | 2026-10-31 | `webbench` 跑出 QPS、`run.sh` 一键脚本 |
| 阶段 C：能讲 + 进简历 | 2026-11-03 | README、GitHub 链接、简历 v2、八问全通 |

> 时间紧，**每个阶段不达标就不往下走**，宁可砍掉改造模块，也要保住「能跑 + 能讲」。

---

## 二、现状与起点（事实）

| 项 | 状态 |
|---|---|
| 代码量 | 2782 行（`http_conn.cpp` 最大，702 行） |
| 编译方式 | `makefile` → `make`，需 `-lmysqlclient` |
| 默认端口 | **9006**（`config.cpp` 第 5 行 `PORT = 9006;`） |
| 启动参数 | `-p` 端口、`-t` 线程数、`-a` 并发模型、`-c` 关日志、`-l` 日志方式… |
| 数据库 | MySQL 运行中（3306），账号 `zql`，库 `yourdb` |
| 运行状态 | `./server` 可跑通，浏览器能打开页面 |
| 已改动 | 端口改过一次 9090，后又改回 9006；`webserver.cpp` 加过 `fprintf` 日志 |

### 项目地图（动手前先认路）

| 文件 | 行数 | 干什么的 | 什么时候动 |
|---|---|---|---|
| `main.cpp` | 41 | 入口，按顺序调 7 个 init | 最后 |
| `config.cpp` | 85 | 命令行参数、默认值 | **第一个动** |
| `webserver.cpp` | 434 | 核心：监听、事件循环、读写分发 | **第二个动** |
| `http/http_conn.cpp` | 702 | HTTP 解析 + 响应拼接 | **第三个动** |
| `threadpool/threadpool.h` | 152 | 线程池 | 理解即可 |
| `timer/lst_timer.cpp` | 224 | 定时器（**升序链表，不是最小堆**） | 理解即可 |
| `lock/locker.h` | 115 | mutex / cond / semaphore 封装 | 理解即可 |
| `log/` | 445 | 日志 + 阻塞队列 | 理解即可 |
| `CGImysql/` | 202 | 数据库连接池 | 理解即可 |

⚠️ **重要提醒**：原版定时器用的是**升序链表**，不是最小堆。简历里如果写「最小堆定时器」，必须真的改过，否则面试会穿帮。

---

## 三、阶段划分

| 阶段 | 主题 | 包含单元 | 产出 |
|---|---|---|---|
| **阶段 0** | 环境打通 | 单元 1–2 | 编译成功、服务启动、页面可访问 |
| **阶段 1** | 看见程序在干活 | 单元 3–4 | 能解释进程/端口/系统调用 |
| **阶段 2** | 第一次改代码 | 单元 5–7 | 改端口、加日志、理解 fd |
| **阶段 3** | 第一次加功能 | 单元 8–9 | `/hello` 路由可访问 |
| **阶段 4** | 性能与工程化 | 单元 10–11 | QPS 数字、`run.sh` |
| **阶段 5** | 改造与包装 | 单元 12–13 | README、简历 v2、八问全通 |

---

## 四、任务清单（含操作、命令、代码、验收标准）

### 单元 1｜环境与编译

**操作**

```bash
cd ~/workspace/projects/TinyWebServer
make clean && make
```

**依赖**：`g++`、`make`、`libmysqlclient-dev`

**验收标准**

- [ ] 编译无 `error`，目录下生成 `server` 文件
- [ ] 能说出 `makefile` 里链接了哪个库（`-lpthread -lmysqlclient`）

---

### 单元 2｜启动与访问

**操作**

```bash
./server
```

另开终端：

```bash
curl -v http://127.0.0.1:9006/
```

**验收标准**

- [ ] 终端输出 `===========Server is running===========`
- [ ] `curl -v` 返回 `HTTP/1.1 200 OK` 且有 HTML
- [ ] 浏览器打开 `http://127.0.0.1:9006` 能看到页面

---

### 单元 3｜进程与端口观察

**操作**

```bash
ss -lntp | grep 9006
ps -eo pid,user,cmd | grep server | grep -v grep
pgrep -ax server
```

**验收标准**

- [ ] 能指出输出里哪个是 **PID**、哪个是**端口**
- [ ] 能解释 `ss` 的 `LISTEN` 状态是什么意思
- [ ] 知道 `kill <PID>` 与 `pkill -9 -f './server'` 的区别

---

### 单元 4｜看见系统调用（strace）

**操作**

```bash
# 先 Ctrl+C 停掉 server
strace -f -e trace=network ./server
```

保持不动，**另开终端** `curl` 一次，回来看输出。

**验收标准**

- [ ] 输出里能找到 `accept`、`read`、`write`
- [ ] 能说出它们的**先后顺序**
- [ ] 能解释「一个请求进来，内核先通知 epoll，再被 accept 接收」这句话

---

### 单元 5｜改默认端口（第一次改代码）

**操作**

打开 `config.cpp`，第 5 行附近：

```cpp
Config::Config(){
    //端口号,默认9006
    PORT = 9006;        // ← 改成 9090
```

```bash
make
./server
curl -I http://127.0.0.1:9090/
curl -I http://127.0.0.1:9006/ --max-time 3      # 应该不通
```

**验收标准**

- [ ] 9090 能访问，9006 不通
- [ ] 能回答：`./server -p 9090` 和改 `config.cpp` 有什么区别？（**答：前者是运行时覆盖，只对本次有效；后者是编译期默认值，`make` 后一直生效**）

---

### 单元 6｜加日志，看见事件流

**操作**

```bash
grep -n "void WebServer::dealwithread" webserver.cpp
grep -n "void WebServer::dealwithwrite" webserver.cpp
```

在两个函数**第一行**各加一句：

```cpp
fprintf(stderr, "[READ ] fd=%d\n", sockfd);
```

```cpp
fprintf(stderr, "[WRITE] fd=%d\n", sockfd);
```

> 用 `fprintf(stderr, ...)` 而不是 `printf`，因为 stderr 不带缓冲，立刻可见。

```bash
make
./server
```

另开终端：

```bash
for i in 1 2 3; do curl -s -o /dev/null http://127.0.0.1:9006/; done
```

**验收标准**

- [ ] 三次 curl 看到**三组** READ/WRITE
- [ ] 能用浏览器打开页面，观察 READ 是否变多
- [ ] 能解释为什么浏览器会比 curl 多（HTML + favicon + 静态资源，每个都是独立请求）

---

### 单元 7｜fd 复用实验

**操作**

```bash
curl -s -o /dev/null http://127.0.0.1:9006/ && curl -s -o /dev/null http://127.0.0.1:9006/
```

**验收标准**

- [ ] 观察到两次的 fd 号**相同**
- [ ] 能解释原因：上一个连接 `close()` 后 fd 被释放，内核分配新 fd 时**优先挑最小空闲编号**

---

### 单元 8｜网站根目录与静态资源映射

**操作**

```bash
ls root/
echo '{"msg":"hello from my server"}' > root/hello.html
curl http://127.0.0.1:9006/hello.html
```

**验收标准**

- [ ] 看到自己写的内容
- [ ] 能说清「URL → `doc_root` + 文件名」这条映射
- [ ] 知道访问一个不存在的文件会返回什么（404）

---

### 单元 9｜加 `/hello` 路由（第一次加功能）

**前置**：先把 `do_request` 函数贴出来，确认改动点。

```bash
grep -n "do_request" http/http_conn.cpp
```

**目标**：访问 `http://127.0.0.1:9006/hello` 返回 `{"msg":"ok"}`。

**思路（二选一，从易到难）**

- 方案 A（简单）：在 `do_request` 里把 `/hello` 映射到 `/hello.html`，文件放 `root/` 下
- 方案 B（进阶）：直接构造响应字符串，不走文件

**验收标准**

- [ ] `curl http://127.0.0.1:9006/hello` 返回 `{"msg":"ok"}`
- [ ] 其他 URL 行为不变（回归测试：`/` 和 `/hello.html` 仍正常）
- [ ] 能讲清这个改动发生在请求处理的哪一步

---

### 单元 10｜压测，拿到真实数字

**操作**

```bash
sudo apt install webbench -y
webbench -c 1000 -t 60 http://127.0.0.1:9006/
```

**验收标准**

- [ ] 拿到 **QPS**、**平均延迟**、**失败请求数**
- [ ] 数字记录到 README 与简历，**不许编**
- [ ] 能说出瓶颈在哪一层（CPU / 日志 I/O / 数据库 / 锁竞争）

**对比实验（可选，加分）**：`./server -l 1`（异步日志）再压一次，比较 QPS 差异。

---

### 单元 11｜`run.sh` 一键脚本

**目标**：编译 → 启动 → 压测 → 出报告 → 关闭，一条命令完成。

```bash
#!/bin/bash
set -e
make -j4
./server > server.log 2>&1 &
PID=$!
sleep 2
webbench -c 1000 -t 30 http://127.0.0.1:9006/ | tee bench.txt
kill $PID
```

**验收标准**

- [ ] `chmod +x run.sh` 后可直接执行
- [ ] 脚本跑完能看到压测结果
- [ ] 脚本结束后进程被清理干净（`pgrep -x server` 无输出）

---

### 单元 12｜改造一个模块 + README

**改造（四选一即可）**

- 加静态文件服务 / 404 错误页
- 日志换成异步（或自己写阻塞队列版）
- 加 Redis 连接池
- 支持 POST + 简单表单处理

**README 必须包含**

- [ ] 架构图（文字版也可以：主线程 → epoll → 线程池 → 业务）
- [ ] 压测数据（环境、并发、QPS、延迟）
- [ ] 踩坑记录（至少 3 条，写你真正遇到的）

**验收标准**

- [ ] GitHub 上有完整代码，能 clone 下来直接 `make`
- [ ] 陌生人看 README 能明白这是什么、怎么跑

---

### 单元 13｜项目八问（最终验收）

见第七节。**标准：不看代码，对着白板讲 10 分钟不卡壳。**

---

## 五、当前进度（截至 2026-10-28）

| 单元 | 状态 | 说明 |
|---|---|---|
| 1 环境与编译 | ✅ 完成 | `make` 通过，`server` 可执行 |
| 2 启动与访问 | ✅ 完成 | 页面能打开 |
| 3 进程与端口 | 🟡 部分 | 会 `ps`/`pgrep`/`ss`，但曾把 `pgrep -af 'server'` 匹配到一堆 `vscode-server` 进程 |
| 4 strace | ❌ 未做 | 还没看过系统调用 |
| 5 改端口 | ✅ 完成 | 改 9090 后又改回 9006，已理解「默认值 vs `-p`」 |
| 6 加日志 | 🟡 部分 | 加了 `fprintf`，但三题答案有偏差 |
| 7 fd 复用 | ❌ 未做 | 实验没跑 |
| 8 root 目录 | ❌ 未做 | |
| 9 `/hello` 路由 | ❌ 未做 | **当前卡在这一步** |
| 10 压测 | ❌ 未做 | |
| 11 run.sh | ❌ 未做 | |
| 12 改造 + README | ❌ 未做 | |
| 13 八问 | ❌ 未做 | |

### 已纠正的三个误解（面试会考）

1. **fd 不是进程。** fd 是进程打开的**文件/套接字的编号**。类比：进程是楼，fd 是房间号。
2. **浏览器不止发一个请求。** HTML、favicon、CSS、JS、图片，每个都是独立请求，所以 READ/WRITE 会明显变多。
3. **结论对、理由错，是最危险的组合。** 比「不会」更容易在面试中被追问到崩。

### 为什么 fd 从 5 开始（高频面试题）

| fd | 是什么 |
|---|---|
| 0 | stdin |
| 1 | stdout |
| 2 | stderr |
| 3 | `listenfd`（socket + bind + listen 得到） |
| 4 | `epollfd`（`epoll_create` 得到） |
| **5** | **第一个 connfd（accept 出来的连接）** |

---

## 六、环境与依赖

### 运行环境

| 项 | 值 |
|---|---|
| 系统 | Ubuntu（VMware 虚拟机，`zql-VMware-Virtual-Platform`） |
| 连接方式 | VSCode Remote-SSH |
| 项目路径 | `~/workspace/projects/TinyWebServer` |
| 虚拟机 IP | `192.168.42.129/24` |
| 网关（VMware NAT） | `192.168.42.2` |
| 宿主机（Windows） | `192.168.42.1` |
| 服务端口 | `9006`（可 `-p` 覆盖） |
| 数据库 | MySQL 3306，账号 `zql` / 库 `yourdb` |

### 编译依赖

```bash
g++ --version
make --version
dpkg -l | grep libmysqlclient
```

缺少时：

```bash
sudo apt install build-essential libmysqlclient-dev -y
```

### 代理（可选，仅终端需要）

宿主机 Clash 端口 **7897**（不是 7890），已开启「允许局域网」。

```bash
ip route | grep default
```

```bash
timeout 3 bash -c 'cat < /dev/null > /dev/tcp/192.168.42.1/7897' && echo "端口通" || echo "端口不通"
```

```bash
curl -x http://192.168.42.1:7897 -I https://www.google.com --max-time 8
```

写入 `~/.bashrc`（**必须放在文件末尾**，否则会被开头的 `case $- in *i*) ;; *) return;; esac` 挡掉）：

```bash
export PROXY_HOST=192.168.42.1
export PROXY_PORT=7897
export http_proxy="http://$PROXY_HOST:$PROXY_PORT"
export https_proxy="$http_proxy"
export no_proxy=localhost,127.0.0.1,::1,192.168.0.0/16

proxy_on() {
  gsettings set org.gnome.system.proxy mode 'manual'
  for p in http https; do
    gsettings set org.gnome.system.proxy.$p host "$PROXY_HOST"
    gsettings set org.gnome.system.proxy.$p port "$PROXY_PORT"
  done
  gsettings set org.gnome.system.proxy ignore-hosts "['localhost','127.0.0.1','::1','192.168.0.0/16']"
  echo "代理已开: $PROXY_HOST:$PROXY_PORT"
}

proxy_off() { gsettings set org.gnome.system.proxy mode 'none'; echo "代理已关"; }
```

```bash
source ~/.bashrc
env | grep -i proxy
```

> `no_proxy` 必须含 `127.0.0.1`，否则访问本机 `9006` 也会走代理然后失败。

---

## 七、验收总表：项目八问

面试官会问这八个，**答不上来 = 没真做**。

- [ ] 1. 为什么用 epoll 而不是 select/poll？
- [ ] 2. ET 和 LT 的区别？ET 为什么必须循环读到 `EAGAIN`？
- [ ] 3. Reactor 和 Proactor 的区别？这个项目是哪种？
- [ ] 4. 线程池怎么实现的？任务队列用什么？怎么唤醒工作线程？
- [ ] 5. 定时器用的什么结构？惰性删除是什么？
- [ ] 6. `EPOLLONESHOT` 解决什么问题？
- [ ] 7. HTTP 怎么判断请求读完了？粘包怎么处理？
- [ ] 8. 压测多少 QPS？瓶颈在哪？

**自测方式**：对着空气讲，手机录音，回听。讲不下去的地方就是缺口。

---

## 八、每日节奏

| 时间 | 内容 |
|---|---|
| 08:00–08:40 | 投递 6 家（与项目并行，不冲突） |
| 08:40–12:00 | **项目**（本计划主体，手机放另一个房间） |
| 13:30–15:30 | 八股 / API 默写（见总计划） |
| 15:30–17:00 | 手写短代码 |
| 19:30–21:00 | 复盘 + 记录进度 |

**每日打卡**

```
第 __ 天        日期 ____

☐ 项目推进了哪个单元：____
☐ 今天改的代码（文件:行）：____
☐ 编译通过 ☐ 运行验证 ☐ 提交 commit
☐ 今天卡住的一件事：____
```

---

## 九、纪律（比技术重要）

1. **改一行、跑一次、看一眼**。不要一次改十行。
2. **改了代码必须 `make`**，`make` 按时间戳判断是否重编。
3. **报错贴原文**，不要说「跑不了」。
4. **卡 20 分钟**就找人问，别硬扛一小时。
5. **不许抄完整代码**——要思路，不要成品。抄来的东西面试一定会露馅。
6. **每个单元完成就 `git commit` 一次**，GitHub 的提交记录本身就是能力证明。

```bash
git add .
git commit -m "feat: 完成单元 N - 简述"
```
