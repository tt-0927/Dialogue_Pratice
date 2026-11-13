/*
 * 文件名:    main.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  main 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/main.cpp */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "server/core/Logger.h"
#include "server/Server.h"

namespace {

/*
 * 功能描述:  执行 usage 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  prog  参数。
 * return:    void
 */
void usage(const char* prog) {
    std::printf(
        "Dialogue Pratice CServer\n"
        "用法: %s [选项]\n"
        "  -b, --bind <addr>        监听地址，默认 0.0.0.0\n"
        "  -p, --port <port>        监听端口，默认 9000\n"
        "      --backlog <n>        listen backlog，默认 512\n"
        "      --timeout <sec>      心跳超时（秒），默认 90\n"
        "      --max-conn <n>       最大连接数，默认 4096\n"
        "      --history <n>        每个房间保留的历史消息条数，默认 200\n"
        "      --room-ttl <sec>     空房间滞留秒数（保留历史），0 = 立即销毁，默认 300\n"
        "      --retention-days <n> 聊天记录保留天数，超期移入 data/archive，0 = 永不清理，默认 90\n"
        "      --require-auth       强制要求账号密码登录（默认允许昵称匿名登录）\n"
        "      --no-register        关闭自助注册（默认开启，公网部署建议关闭）\n"
        "      --log-level <lvl>    debug|info|warn|error，默认 info\n"
        "  -h, --help               显示帮助\n",
        prog);
}

/*
 * 功能描述:  执行 takeValue 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  argc  参数。
 * param [in]  argv  参数。
 * param [out]  i  参数。
 * param [in]  name  参数。
 * return:    返回值，含义见功能描述。
 */
const char* takeValue(int argc, char** argv, int& i, const char* name) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "缺少参数值: %s\n", name);
        std::exit(2);
    }
    return argv[++i];
}

/* namespace */
}

/*
 * 功能描述:  执行 main 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  argc  参数。
 * param [in]  argv  参数。
 * return:    返回值，含义见功能描述。
 */
int main(int argc, char** argv) {
    dp::CServer::Config cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];

        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (a == "-b" || a == "--bind") {
            cfg.bindAddr = takeValue(argc, argv, i, "--bind");
            continue;
        }
        if (a == "-p" || a == "--port") {
            cfg.port = static_cast<uint16_t>(std::atoi(takeValue(argc, argv, i, "--port")));
            continue;
        }
        if (a == "--backlog") {
            cfg.backlog = std::atoi(takeValue(argc, argv, i, "--backlog"));
            continue;
        }
        if (a == "--timeout") {
            cfg.heartbeatTimeoutSec = std::atoi(takeValue(argc, argv, i, "--timeout"));
            continue;
        }
        if (a == "--max-conn") {
            cfg.maxConnections = static_cast<size_t>(std::atol(takeValue(argc, argv, i, "--max-conn")));
            continue;
        }
        if (a == "--history") {
            cfg.historyEntries = static_cast<size_t>(std::atol(takeValue(argc, argv, i, "--history")));
            continue;
        }
        if (a == "--room-ttl") {
            cfg.roomTtlSec = std::atoi(takeValue(argc, argv, i, "--room-ttl"));
            continue;
        }
        if (a == "--retention-days") {
            cfg.messageRetentionDays = std::atoi(takeValue(argc, argv, i, "--retention-days"));
            continue;
        }
        if (a == "--require-auth") {
            cfg.requireAuth = true;
            continue;
        }
        if (a == "--no-register") {
            cfg.allowRegistration = false;
            continue;
        }
        if (a == "--log-level") {
            dp::LogLevel_EN lv;
            if (!dp::CLogger::parseLevel(takeValue(argc, argv, i, "--log-level"), lv)) {
                std::fprintf(stderr, "非法日志级别，可选 debug|info|warn|error\n");
                return 2;
            }
            dp::CLogger::instance().setLevel(lv);
            continue;
        }

        std::fprintf(stderr, "未知参数: %s\n", a.c_str());
        usage(argv[0]);
        return 2;
    }

    if (cfg.port == 0) {
        std::fprintf(stderr, "端口不能为 0\n");
        return 2;
    }
    if (cfg.heartbeatTimeoutSec <= 0) {
        std::fprintf(stderr, "心跳超时必须为正数\n");
        return 2;
    }

    dp::CServer server(cfg);
    if (!server.start()) {
        LOG_ERROR("server start failed");
        return 1;
    }
    server.run();
    LOG_INFO("bye");
    return 0;
}
