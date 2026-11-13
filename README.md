# Dialogue Pratice — 服务端

局域网英语对话练习 App 的服务端。C++17 + POSIX socket + epoll，单线程 reactor，
按「房间号」组织会话，同房间内消息实时互通。Qt6 客户端为下一阶段内容。

- 运行环境：Ubuntu 22.04 / g++ 11.4.0 / CMake 3.22（Linux 专属，依赖 epoll）
- 部署路径：`/home/jatin/Code/Dialogue_Pratice`（`/Code` 已是指向 `~/Code` 的软链，两种写法等价）
- 监听：`0.0.0.0:9000`

---

## 1. 当前状态

| 部分 | 状态 |
| --- | --- |
| 服务端（登录 / 房间 / 广播 / 心跳 / 优雅退出） | 已完成，测试全绿 |
| 协议层 `common/`（服务端与 Qt 客户端共用） | 已完成 |
| Qt6 客户端 | 已完成，见 `../Dialogue_Pratice_Client` |

服务端**刻意不使用 Qt**：`QTcpServer` 自带事件循环，无法把连接托管给自定义 epoll；
用 `QSocketNotifier` 包装 fd 只会把简单的 reactor 绕复杂。Qt6 的价值在 GUI 客户端，
协议层 `common/Protocol.h` 不依赖任何 POSIX 接口，客户端可直接复用同一份编解码代码。

---

## 2. 目录结构

服务端按职责分三层，依赖方向单向：`business/media → core → common`。
`core` 不反向依赖业务层。

```
Dialogue_Pratice/
├── CMakeLists.txt              # 顶层，C++17，-Wall -Wextra -Wpedantic
├── common/                     # 两端共用的协议层（无 POSIX 依赖）
│   ├── Protocol.h              # opcode 定义、组帧、增量解帧器、语音约束
│   ├── Protocol.cpp
│   └── Base64.h                # 语音消息的 Base64 编解码
│
├── server/
│   ├── main.cpp                # 命令行参数解析
│   ├── Server.{h,cpp}          # 组合根：装配 Reactor + 注册业务服务
│   │
│   ├── core/                   # 网络与基础设施（不含业务语义）
│   │   ├── Reactor.{h,cpp}     # epoll 事件循环、连接生命周期、出站缓冲
│   │   ├── ServerContext.{h,cpp}  # 共享状态（配置/会话/房间/序号）+ 出站能力
│   │   ├── ServerConfig.h      # 启动参数
│   │   ├── Service.h           # 业务服务抽象基类
│   │   ├── ServiceRegistry.{h,cpp} # opcode → Service 映射（单例）
│   │   ├── Session.{h,cpp}     # 一条连接：身份状态 + 收发缓冲
│   │   ├── SessionManager.{h,cpp}  # 会话容器与 id 分配
│   │   ├── Room.{h,cpp}        # 房间：成员集合 + 历史缓冲 + 广播
│   │   ├── RoomManager.{h,cpp} # 房间容器，空房间滞留与回收
│   │   ├── OutBuffer.h         # 非阻塞发送缓冲
│   │   ├── Logger.{h,cpp}      # 带时间戳/级别的日志（单例）
│   │   ├── Validation.h        # 字段校验（昵称/房间号/正文/简介）
│   │   ├── TimeUtil.h          # nowMs()
│   │   ├── Crypto.{h,cpp}      # SHA-256 + 随机盐（密码哈希）
│   │   ├── Database.{h,cpp}    # SQLite 的 RAII 封装
│   │   ├── UserRepository.{h,cpp}    # 用户表读写
│   │   └── MessageRepository.{h,cpp} # 聊天记录读写
│   │
│   ├── business/               # 业务规则
│   │   ├── UserService.{h,cpp} # 登录 / 改资料 / 心跳
│   │   ├── RoomService.{h,cpp} # 进房 / 离房 / 房间列表 / 历史回放
│   │   └── ChatService.{h,cpp} # 文本消息
│   │
│   └── media/                  # 媒体内容
│       └── VoiceService.{h,cpp}# 语音消息（将来 RTC 的扩展点）
│
├── data/                       # 运行时生成，不入库
│   ├── users.db                # 用户库
│   └── messages.db             # 聊天记录库
├── third_party/nlohmann/json.hpp   # 单头文件 JSON（v3.11.3，已内置）
├── third_party/sqlite3/        # SQLite amalgamation（scripts/setup_sqlite.sh 拉取）
├── scripts/
│   ├── build.sh                # 一键构建
│   ├── run.sh                  # 一键启动
│   ├── smoke_test.py           # 24 项协议级功能测试
│   ├── load_test.py            # 并发压测
│   └── churn_test.py           # 建连/断连抖动 + fd 泄漏检查
├── .vscode/                    # Remote-SSH 下直接 F5 调试
└── logs/server.log             # 运行日志（运行时生成）
```

### 2.1 一次请求的处理路径

```
epoll_wait
  └─ Reactor::handleReadable      读干 socket，解出完整帧
      └─ Reactor::dispatch        解析 JSON
          └─ ServiceRegistry::dispatch
              └─ ChatService / RoomService / UserService / VoiceService
                  ├─ 校验（core/Validation.h）
                  ├─ 落历史（Room::appendHistory）
                  └─ ServerContext::broadcastRoom
                      └─ Reactor::broadcastRoom   帧只编码一次，逐个入队
```

**Reactor 不含任何 opcode 分支**：新增一种请求只需写一个 `Service` 派生类并注册，
事件循环一行都不用改。

### 2.2 生命周期

| 类型 | 策略 |
| --- | --- |
| `Logger`、`ServiceRegistry` | 进程级单例（函数内静态对象） |
| `Session`、`Room` | 由各自的 Manager 用 `std::unique_ptr` 独占 |
| `ServerContext`、`Reactor` | 由 `Server` 用 `std::unique_ptr` 独占 |
| 业务 `Service` | 由 `ServiceRegistry` 用 `std::unique_ptr` 独占 |

外部只拿裸指针观察，从不负责释放。

---

## 3. 快速开始

```bash
cd ~/Code/Dialogue_Pratice

./scripts/build.sh                       # 构建 -> build/bin/dialogue_server
./scripts/run.sh                         # 前台启动，Ctrl-C 优雅退出
./scripts/run.sh --port 9001 --log-level debug

# 另开终端
python3 scripts/smoke_test.py            # 功能自检（21 项）
python3 scripts/load_test.py --clients 500 --rooms 10 --messages 20
python3 scripts/churn_test.py --rounds 150 --batch 20
```

后台常驻：

```bash
setsid ./build/bin/dialogue_server --port 9000 > logs/server.log 2>&1 < /dev/null &
pkill -x dialogue_server                 # 停止（注意用 -x，-f 会误杀含同名字符串的 shell）
```

命令行参数：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `-b, --bind` | `0.0.0.0` | 监听地址 |
| `-p, --port` | `9000` | 监听端口 |
| `--backlog` | `512` | listen backlog |
| `--timeout` | `90` | 心跳超时（秒），超过即断开 |
| `--max-conn` | `4096` | 最大并发连接数 |
| `--history` | `200` | 每房间保留的历史消息条数 |
| `--room-ttl` | `300` | 空房间滞留秒数（保留历史），0 = 立即销毁 |
| `--log-level` | `info` | `debug\|info\|warn\|error` |

---

## 4. 架构

### 4.1 事件流

```
                    ┌──────────────────────────────────────────┐
   accept4(ET) ────► │  epoll_wait  (单线程，timeout=1000ms)     │
                    └───────┬──────────────────────────┬───────┘
                            │ EPOLLIN                  │ 每秒一次
                            ▼                          ▼
                 recv 循环到 EAGAIN              心跳扫描 / 统计日志
                            │
                    FrameDecoder 累积
                            │ 完整帧
                            ▼
        ServiceRegistry::dispatch ──► UserService / RoomService / ChatService / VoiceService
                            │
                            ▼
              Room::broadcast ──► 逐成员 enqueue + flush
                            │
                    flush 返回 Blocked？
                       ├─ 否 → 完成
                       └─ 是 → EPOLL_CTL_MOD 加上 EPOLLOUT，等可写再续写
```

### 4.2 关键设计决策

1. **边沿触发（EPOLLET）+ 读干写干。** 监听 fd 与连接 fd 全部注册 `EPOLLIN|EPOLLET|EPOLLRDHUP`。
   边沿触发要求每次事件必须把内核缓冲读空（`recv` 循环到 `EAGAIN`），否则残留数据不会再触发事件——
   这是 epoll 最常见的 bug，`Session::readAvailable()` 与 `Reactor::handleAccept()` 都按此实现。
   代价是逻辑比水平触发复杂，收益是事件次数与消息量解耦（1 万条消息一次事件即可处理完）。

2. **发送缓冲 + EPOLLOUT 惰性订阅。** `send()` 在非阻塞 socket 上可能只写出一部分，
   剩余部分进 `OutBuffer`，并仅在「有积压」时把 `EPOLLOUT` 加入关注（`updateInterest()`），
   写空后立刻摘掉。这样既不会丢消息，也不会让每条消息都产生一次 `epoll_ctl`。

3. **延迟回收（deferred close）。** 广播过程中若某个成员写失败，不能当场 `delete` 它的 Session——
   房间的 `members_` 迭代器还在用。所有关闭动作只标记 `SessionState::Closing` 并压入
   `pendingClose_`，等本批事件处理完由 `Reactor::reapSessions()` 统一销毁。关闭本身可能级联
   （A 关闭时广播，导致 B 写失败），所以回收用「批次交换」循环，而非一次遍历。

4. **epoll 里存 session id，不存 fd。** `epoll_event.data.u64 = session->id()`。
   若存 fd，一旦在事件批次内关闭旧连接、又 accept 到复用了同一 fd 的新连接，
   后续事件会被错误地派发给新连接（经典 fd 复用竞态）。id 单调递增不复用，天然免疫。

5. **房间生命周期跟随成员。** 房间不需要显式创建：第一个 `JoinRoom` 时 `getOrCreate`，
   最后一人离开（含掉线）即销毁。避免「空房间」无限堆积。

6. **信号统一走 signalfd。** `SIGINT/SIGTERM/SIGPIPE` 先 `sigprocmask` 屏蔽，再注册进 epoll。
   信号不会异步打断 `epoll_wait`，退出路径与普通事件完全一致，`Ctrl-C` 能干净地关闭全部连接。

### 4.3 类职责

| 类 | 层 | 职责 | 不该做的事 |
| --- | --- | --- | --- |
| `Server` | 组合根 | 创建上下文与事件循环、注册服务、注入关闭钩子 | 不含任何业务逻辑 |
| `Reactor` | core | epoll 循环、连接生命周期、出站缓冲 | 不含任何 opcode 分支 |
| `ServerContext` | core | 共享状态 + 出站能力（send/broadcast/close） | 不实现业务规则 |
| `ServiceRegistry` | core | opcode → Service 映射（单例） | 不关心服务内部逻辑 |
| `Session` | core | 一条连接的读写、解帧、身份与所在房间 | 不决定「谁能收到消息」 |
| `SessionManager` | core | id 分配、Session 容器 | 不处理业务 |
| `Room` | core | 成员集合、历史缓冲、广播 | 不碰 socket |
| `RoomManager` | core | 房间容器、空房回收 | 不处理成员进出 |
| `FrameDecoder` | common | 字节流 → 帧（粘包/半包） | 不解析 JSON 语义 |
| `UserService` | business | 登录 / 改资料 / 心跳 | 不碰房间与消息 |
| `RoomService` | business | 进房 / 离房 / 列表 / 历史回放 | 不解析消息内容 |
| `ChatService` | business | 文本消息校验与投递 | 不处理语音 |
| `VoiceService` | media | 语音消息校验与转发 | 不解码 Opus |

---

### 4.4 分层与扩展点

```
common/Protocol.h        协议编解码（两端唯一实现）
      ▲
core/                    Reactor / ServerContext / Session / Room / ServiceRegistry
      ▲
business/ media/         UserService / RoomService / ChatService / VoiceService
      ▲
Server                  组合根：创建 ctx、reactor，注册服务，注入关闭钩子
```

扩展新请求类型的步骤：

1. 在 `common/Protocol.h` 加 opcode；
2. 写一个 `Service` 派生类，实现 `opcodes()` 与 `handle()`；
3. 在 `Server::start()` 里 `registry.registerService(...)`。

`Reactor` 与 `ServiceRegistry` 都不用改。语音消息就是按这个流程加的，
将来的实时音视频可以照同样方式接入 `media/`。

### 4.5 LLM 配置下发

服务端可以在客户端登录时下发一份完整的 LLM 配置，让客户端不必各自维护 `llm.json`。

**配置来源**：服务端工作目录下的 `config/llm.json`（路径由 `ServerConfig::llmConfigPath` 指定，
默认 `config/llm.json`，留空则不下发）。启动时读一次，日志会打印：

```
INFO  LLM 配置已加载（config/llm.json），登录时下发给客户端
```

**下发方式**：`LoginResp` 里多一个 `llmConfig` 字段，内容原样透传：

```json
{
  "userId": 12, "name": "alice", "ok": true,
  "llmConfig": {
    "llm":        { "baseUrl": "...", "apiKey": "...", "model": "..." },
    "translation": { "llm": { ... } }
  }
}
```

服务端**不解析**这个对象——结构由客户端定义，服务端只负责搬运。这样换模型、加字段
都不需要动服务端代码。

**向后兼容**：客户端收不到 `llmConfig` 时回退读本地文件；老服务端不下发该字段，新客户端照常工作。

**改完要重启服务端**（配置在 `Server::start()` 时读一次）。

---

### 4.6 数据库

服务端用 **SQLite** 持久化两类数据，分成两个独立文件——它们的备份频率和增长曲线完全不同，
分开更利于运维。

技术选择：**官方 amalgamation 单文件嵌入**（`third_party/sqlite3/sqlite3.c`），
不依赖系统 `libsqlite3-dev`。目标机只有运行时库（`libsqlite3.so.0`）也能编译。

拉取：`./scripts/setup_sqlite.sh`（自动找最新版）

#### users.db —— 用户库

| 字段 | 说明 |
| --- | --- |
| `id` | 持久用户 id，登录后随 `LoginResp.accountId` 返回 |
| `username` | 登录名，唯一 |
| `password_hash` / `salt` | 密码哈希，**不存明文** |
| `nickname` | 显示昵称 |
| `age` / `description` | 资料 |
| `enabled` | 是否允许登录（封禁用） |
| `created_at` / `last_login_at` | 时间戳 |

密码方案：`sha256(salt + ":" + password)`，盐 16 字节取自 `/dev/urandom`。
校验用常量时间比较，避免时间侧信道。**没有做密钥拉伸**（PBKDF2/Argon2），
教学场景够用；面向公网应升级。

首次启动若表为空，自动插入 4 个测试账号（初始密码统一 `123456`）：

| 登录名 | 昵称 | 年龄 |
| --- | --- | --- |
| `alice` | alice | 24 |
| `bob` | bob | 28 |
| `carol` | carol | 22 |
| `dave` | dave | 31 |

**登录匹配规则**：先按 `username` 精确匹配，再按 `nickname` 忽略大小写匹配；
都没命中则走「匿名登录」（保留原有「随便填昵称」的兼容行为）。

#### messages.db —— 聊天记录库

| 字段 | 说明 |
| --- | --- |
| `room_id` / `seq` | 联合唯一，重复写入静默忽略（`INSERT OR IGNORE`） |
| `sender_id` | 发送者的**连接** id（与协议 `from` 对齐，每次连接不同） |
| `account_id` | 发送者的**持久**用户 id，0 = 匿名 |
| `sender_name` | 昵称快照：改名后历史仍显示当时的名字 |
| `kind` | 0 = 文本，1 = 语音 |
| `text` / `audio` / `duration_ms` | 内容 |
| `created_at` | 时间戳 |

索引：`(room_id, seq)`、`account_id`、`created_at`。

**写入时机**：`ChatService` / `VoiceService` 在「进内存缓冲之前」落库，落库失败只记日志、
不影响聊天——消息服务不该被存储问题拖垮。

**读取时机**：进房时若内存环形缓冲为空（服务端刚重启），从数据库补回最近 100 条。
这样重启后历史不会凭空消失，数据库也不会变成「只写不读」。

验证工具：

```bash
python3 scripts/check_history.py --room 1001
# 房间 1001：文本 2 条，语音 0 条
#   - hello 世界
#   - hi alice
```

#### 4.6.1 数据保留与归档

聊天记录会无限增长，所以主库只留「最近 N 天」：

| 配置 | 默认 | 说明 |
| --- | --- | --- |
| `messageRetentionDays` | 90 | 保留天数，`0` = 永不清理 |
| `archiveDir` | `data/archive` | 归档库目录 |

命令行：`--retention-days 30`

**归档而不是删除**：超期记录先 `INSERT OR IGNORE` 到
`data/archive/messages_archive.db`（结构与主库一致），再从主库 `DELETE`。

用 SQLite 的 `ATTACH` 在一条连接里完成跨库复制，不必把数据读进内存：

```sql
ATTACH DATABASE 'archive.db' AS arch;
INSERT OR IGNORE INTO arch.messages SELECT ... FROM main.messages WHERE created_at < ?;
DELETE FROM main.messages WHERE created_at < ?;
DETACH DATABASE arch;
```

**先复制后删除**：中途失败最多留下重复记录（唯一键 + `IGNORE` 保护），不会丢数据。

**执行时机**：服务端启动时一次，之后每 24 小时一次（`Reactor` 每秒回调，
`Server` 自己判断是否到点——Reactor 不需要理解业务周期）。

实测：

```
INFO  聊天记录库就绪：data/messages.db（6 条记录）
INFO  归档完成：3 条超过 90 天的记录已移入 data/archive/messages_archive.db，主库剩余 3 条
```

> 注意：房间历史现在会持久化，**自检脚本改用随机房间号**（`ROOM_SUFFIX`），
> 否则会撞上上次运行留下的历史记录。

---

### 4.7 登录认证

`Login` 请求支持可选的 `password` 字段，服务端据此分三条路径：

| 客户端行为 | 服务端处理 |
| --- | --- |
| 带 `password` | `verifyCredentials()` 校验；失败统一返回 `auth_failed` |
| 不带 `password`，`requireAuth=false`（默认） | 按 `username` 精确 → `nickname` 忽略大小写匹配，都不中则匿名 |
| 不带 `password`，`requireAuth=true` | 返回 `auth_required` |

命令行：`--require-auth` 开启强制认证。

**为什么默认不强制**：现有客户端只发昵称，强制会直接把它们挡在门外。
默认的兼容模式让老客户端照常工作，同时新客户端可以立刻用上账号密码。

**错误提示统一**：账号不存在与密码错误都返回 `auth_failed` + 「账号或密码错误」，
不区分两者，避免账号枚举攻击。

**重连也要密码**：`MainWindow` 把账号密码记在内存里（`pendingName_` / `pendingPassword_`），
断线重连时直接复用——否则强制认证模式下重连会全部失败。

验证：

```bash
python3 scripts/test_auth.py
# [PASS] 正确账号密码登录成功  accountId=1
# [PASS] 错误密码被拒绝  code=auth_failed
# [PASS] 不存在的账号被拒绝且提示一致  code=auth_failed
# [PASS] 不带密码走兼容模式成功  accountId=1
```

### 4.8 自助注册

`Register` (0x08) → `RegisterResp` (0x8B)。

```json
// 请求（username / password 必填，其余可选）
{ "username": "bob", "password": "secret123",
  "nickname": "Bob", "age": 28, "desc": "英语母语者" }

// 成功
{ "ok": true, "userId": 9, "username": "bob", "nickname": "Bob" }
```

**注册不自动登录**：成功后客户端把账号密码回填到登录表单，用户点一次登录。
这样「注册」和「登录」两条路径完全独立，出问题好排查。

校验规则与错误码：

| 条件 | 错误码 |
| --- | --- |
| 缺 `username` / `password` | `bad_payload` |
| 登录名不是 3–24 位 `[A-Za-z0-9_-]` | `bad_username` |
| 密码不是 6–64 位 | `weak_password` |
| 昵称超 24 字节或含控制字符 | `bad_name` |
| 登录名已存在 | `username_taken` |
| 已登录状态下注册 | `already_logged_in` |
| 同一条连接第 4 次尝试 | `too_many_registrations` |

**防刷**：单条连接最多 3 次注册尝试。够挡住「一个脚本批量刷号」这种最低成本攻击，
但挡不住换连接重试——真要防需要在网关层按 IP 限流。

**开关**：`--no-register` 关闭自助注册。默认开启（局域网/教学场景方便），
公网部署建议关掉，改由管理员建号。

验证：

```bash
python3 scripts/test_register.py
# [PASS] 正常注册成功  userId=8
# [PASS] 新账号可登录  accountId=8
# [PASS] 已登录后注册被拒绝  code=already_logged_in
# [PASS] 重名被拒绝  code=username_taken
# [PASS] 用户名过短被拒绝  code=bad_username
# [PASS] 用户名含非法字符被拒绝  code=bad_username
# [PASS] 密码过短被拒绝  code=weak_password
# [PASS] 单连接注册次数超限被拒绝  code=too_many_registrations
```

---

## 5. 协议

### 5.1 帧格式（大端）

```
+------------------+-------------+----------------------------+
|  length: uint32  |  op: uint8  |  payload: JSON (length-1)  |
+------------------+-------------+----------------------------+
```

`length = 1 + payload.size()`，单帧上限 64 KiB + 1。用长度前缀而非 `\n` 分隔，
彻底规避 TCP 粘包/半包与分隔符注入；`length` 越界直接判定 `protocol_error` 并断开。

### 5.2 操作码

| 上行 | 值 | payload | 下行 | 值 | payload |
| --- | --- | --- | --- | --- | --- |
| `Login` | 0x01 | `{"name":"alice","age":23,"desc":"..."}` | `LoginResp` | 0x81 | `{"ok":true,"userId":1,"name":"alice","age":23,"desc":"","serverTime":...}` |
| `JoinRoom` | 0x02 | `{"room":"1001"}` | `RoomState` | 0x82 | `{"room":"1001","created":false,"members":[{"id":1,"name":"alice"}]}` |
| `LeaveRoom` | 0x03 | `{}` | `ChatPush` | 0x83 | `{"seq":12,"from":1,"name":"alice","room":"1001","text":"hi","ts":...}` |
| `Chat` | 0x04 | `{"text":"hi"}` | `SystemMsg` | 0x84 | `{"event":"join\|leave\|left\|welcome\|rename","room":..,"text":..,"ts":..}` |
| `ListRooms` | 0x05 | `{}` | `ErrorMsg` | 0x85 | `{"code":"bad_room","text":"..."}` |
| `Ping` | 0x06 | `{}` | `Pong` | 0x86 | `{"ts":...}` |
| `UpdateProfile` | 0x07 | `{"name":"alice","age":24,"desc":"..."}` | `RoomList` | 0x87 | `{"rooms":[{"id":"1001","count":2}]}` |
| | | | `ProfileResp` | 0x88 | `{"ok":true,"userId":1,"name":..,"age":..,"desc":..}` |
| | | | `HistoryResp` | 0x89 | `{"room":"1001","messages":[{"seq":12,"from":1,"name":"alice","text":"hi","ts":..}]}` |

### 5.3 约定

- **`ChatPush` 会回显给发送者**。客户端无需本地插入消息，显示顺序与服务端广播顺序天然一致。
- `Chat` 只在当前所在房间广播，跨房间不可见；`JoinRoom` 到新房间会自动先退出旧房间并通知旧房友。
- **房间不需要预先创建**：`JoinRoom` 时房间不存在则按房间号创建，`RoomState.created` 告知客户端本次是新建还是加入。
- `UpdateProfile` 只带需要改的字段；改名成功后向同房间广播 `SystemMsg(event=rename)`，避免成员列表停留在旧名字。
- **进房后服务端补发历史**：`RoomState` 之后紧跟 `HistoryResp`（最近 100 条 / 40 KB 以内，时间正序）。
  `ChatPush` 与历史条目都带全局单调 `seq`，客户端据此去重、补齐断线期间的消息。
- 房间成员走空后不立即销毁，滞留 `--room-ttl` 秒（默认 300）以保留历史，期间 `RoomList` 不展示该房间。
- 所有下行消息都带 `ts`（Unix 毫秒）。
- 成员列表按 `userId` 升序，输出稳定、便于断言。

### 5.4 错误码

| code | 触发条件 |
| --- | --- |
| `not_logged_in` | 未 `Login` 就执行房间/发言操作 |
| `already_logged_in` | 重复 `Login` |
| `bad_payload` / `bad_json` | 缺字段、类型不对、JSON 非法 |
| `bad_name` | 名字为空 / 超 24 字节 / 含控制字符 |
| `bad_age` | 年龄不在 0..150（0 表示未填写） |
| `bad_desc` | 个人描述超 200 字节或含控制字符 |
| `bad_room` | 房间号非 `[A-Za-z0-9_-]{1,32}` |
| `not_in_room` | 未进房就发言 / 未进房就离开 |
| `bad_text` | 消息为空或超 4000 字节 |
| `protocol_error` | 帧长度非法（随后断开） |

---

## 6. 实测数据

测试机：Ubuntu 22.04 虚拟机，16 vCPU，g++ 11.4.0 Release。

| 场景 | 结果 |
| --- | --- |
| 编译 | `-Wall -Wextra -Wpedantic` 下 **0 warning** |
| 功能自检 `smoke_test.py` | **21/21 通过**（含中文 UTF-8、跨房隔离、逐字节半包、两帧粘包、非法帧、资料更新与改名广播、历史消息与空房滞留） |
| 500 连接 / 10 房间 / 各 20 条 | 500,000 条投递全部相符，9.05 s，**55,245 msg/s**，RSS **4.3 MB** |
| 2000 连接 / 20 房间 / 各 5 条 | 1,000,000 条投递全部相符，27.64 s，**36,176 msg/s**，RSS **7.2 MB** |
| 建连/断连抖动 3000 次 | fd **6 → 6**（无泄漏），RSS 3,340 → 3,528 KB，3000 条 disconnected 日志一一对应 |
| ASan + UBSan（功能 + 抖动 + 300 并发压测） | **0 错误、0 泄漏** |
| 进程线程数 | **1**（单线程 reactor，全部状态无需加锁） |

关于吞吐：2000 连接场景的吞吐低于 500 连接，瓶颈在 **Python 测试客户端**——
单线程 asyncio 要解析 100 万条 JSON，服务端 CPU 并未跑满。这组数字用于验证正确性与内存占用，
不作为服务端性能上限。

---

## 7. 已知限制

1. **明文、无鉴权**：仅适合内网 / 教学环境，公网部署需加 TLS（OpenSSL 或 nginx 前置）。
2. **历史消息是内存态**：每房间保留最近 200 条 / 64 KB，进程重启即清空；房间状态全在内存。
3. **无速率限制**：单个客户端可高频刷屏，需按 Session 加令牌桶。
4. **广播是 O(K) 逐连接写**：房间人数上千时建议合并写（把同一帧的 `send` 合并为一次 `writev`）。
5. **无消息序号/去重**：依赖 TCP 有序性，断线重连不补发历史消息。
6. **C++ 标准为 17**：g++ 11 对 C++20/23 支持不完整，故未使用 `std::span`、`std::format` 等。

---

## 8. Qt6 客户端（已完成）

客户端位于同级目录 `../Dialogue_Pratice_Client`，Qt 6.2 Widgets + Telegram 风格界面，
通过 `-DSHARED_ROOT` 直接复用本工程的 `common/` 协议层。

```bash
sudo apt install -y qt6-base-dev libgl1-mesa-dev libopengl-dev
cd ~/Code/Dialogue_Pratice_Client
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"
./build/bin/dialogue_client
```

界面：登录（服务器/昵称）→ 主页（个人信息、编辑资料、房间号进房）→ 聊天（气泡、日期分隔、
系统提示、成员侧栏）。详细说明见该目录的 `README.md`。

---

## 9. VS Code Remote-SSH

1. 安装扩展 `Remote - SSH`，`F1` → `Remote-SSH: Connect to Host` → `jatin@192.168.80.128`。
2. 打开文件夹 `/home/jatin/Code/Dialogue_Pratice`（即 `~/Code/Dialogue_Pratice`）。
3. `Ctrl+Shift+B` 触发 `cmake: build (Release)`；`F5` 以 gdb 启动（`.vscode/launch.json` 已配好）。
4. 需要代码跳转时安装 `C/C++` 扩展，`c_cpp_properties.json` 已把 `third_party` 加进 includePath。
