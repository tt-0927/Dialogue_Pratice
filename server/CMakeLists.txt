# 服务端可执行文件。
#
# 分层结构：
#   core/      网络与基础设施（事件循环、会话、房间容器、日志、服务注册）
#   business/  业务规则（用户、房间、文本消息）
#   media/     媒体内容（语音消息；将来 RTC 的扩展点）
#
# 依赖方向是单向的：business/media -> core -> common。core 不反向依赖业务层。

# SQLite amalgamation：单文件嵌入，不依赖系统 libsqlite3-dev。
# 第三方代码用 -w 关掉警告，否则 -Wall -Wextra 下会刷屏。
add_library(dp_sqlite3 STATIC ${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c)
target_include_directories(dp_sqlite3 PUBLIC ${CMAKE_SOURCE_DIR}/third_party/sqlite3)
target_compile_definitions(dp_sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dp_sqlite3 PRIVATE -w)
endif()

add_executable(dialogue_server
    main.cpp
    Server.cpp
    # ---- core ----
    core/Logger.cpp
    core/Crypto.cpp
    core/Database.cpp
    core/UserRepository.cpp
    core/MessageRepository.cpp
    core/Session.cpp
    core/SessionManager.cpp
    core/Room.cpp
    core/RoomManager.cpp
    core/ServerContext.cpp
    core/Reactor.cpp
    core/ServiceRegistry.cpp
    # ---- business ----
    business/UserService.cpp
    business/RoomService.cpp
    business/ChatService.cpp
    # ---- media ----
    media/VoiceService.cpp
)

target_link_libraries(dialogue_server PRIVATE
    dialogue_common
    dp_sqlite3
    pthread
)
