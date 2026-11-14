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
 * 功能描述:  打印命令行参数用法说明。
 * 作者:      JaTin
 * param [in]   strProg          输入数据，类型 const char*。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void usage(const char* strProg) {
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
        strProg);
}

/*
 * 功能描述:  从命令行参数中取出指定选项的值。
 * 作者:      JaTin
 * param [in]   nArgc            输入数据，类型 int。
 * param [in]   strArgv          输入数据，类型 char**。
 * param [out]  nI               输入数据，类型 int&。
 * param [in]   strName          名称或昵称，类型 const char*。
 * return:    成功返回对象指针；未找到或失败时返回 nullptr。
 */
const char* takeValue(int nArgc, char** strArgv, int& nI, const char* strName) {
    if (nI + 1 >= nArgc) {
        std::fprintf(stderr, "缺少参数值: %s\n", strName);
        std::exit(2);
    }
    return strArgv[++nI];
}

/* namespace */
}

/*
 * 功能描述:  程序入口：解析命令行参数、初始化服务端并运行事件循环。
 * 作者:      JaTin
 * param [in]   nArgc            输入数据，类型 int。
 * param [in]   strArgv          输入数据，类型 char**。
 * return:    返回处理结果码；0 表示成功，负值或 -1 表示失败。
 */
int main(int nArgc, char** strArgv) {
    dp::CServer::Config stCfg;

    for (int nI = 1; nI < nArgc; ++nI) {
        const std::string strA = strArgv[nI];

        if (strA == "-h" || strA == "--help") {
            usage(strArgv[0]);
            return 0;
        }
        if (strA == "-b" || strA == "--bind") {
            stCfg.bindAddr = takeValue(nArgc, strArgv, nI, "--bind");
            continue;
        }
        if (strA == "-p" || strA == "--port") {
            stCfg.port = static_cast<uint16_t>(std::atoi(takeValue(nArgc, strArgv, nI, "--port")));
            continue;
        }
        if (strA == "--backlog") {
            stCfg.backlog = std::atoi(takeValue(nArgc, strArgv, nI, "--backlog"));
            continue;
        }
        if (strA == "--timeout") {
            stCfg.heartbeatTimeoutSec = std::atoi(takeValue(nArgc, strArgv, nI, "--timeout"));
            continue;
        }
        if (strA == "--max-conn") {
            stCfg.maxConnections = static_cast<size_t>(std::atol(takeValue(nArgc, strArgv, nI, "--max-conn")));
            continue;
        }
        if (strA == "--history") {
            stCfg.historyEntries = static_cast<size_t>(std::atol(takeValue(nArgc, strArgv, nI, "--history")));
            continue;
        }
        if (strA == "--room-ttl") {
            stCfg.roomTtlSec = std::atoi(takeValue(nArgc, strArgv, nI, "--room-ttl"));
            continue;
        }
        if (strA == "--retention-days") {
            stCfg.messageRetentionDays = std::atoi(takeValue(nArgc, strArgv, nI, "--retention-days"));
            continue;
        }
        if (strA == "--require-auth") {
            stCfg.requireAuth = true;
            continue;
        }
        if (strA == "--no-register") {
            stCfg.allowRegistration = false;
            continue;
        }
        if (strA == "--log-level") {
            dp::LogLevel_EN enLv;
            if (!dp::CLogger::parseLevel(takeValue(nArgc, strArgv, nI, "--log-level"), enLv)) {
                std::fprintf(stderr, "非法日志级别，可选 debug|info|warn|error\n");
                return 2;
            }
            dp::CLogger::instance().setLevel(enLv);
            continue;
        }

        std::fprintf(stderr, "未知参数: %s\n", strA.c_str());
        usage(strArgv[0]);
        return 2;
    }

    if (stCfg.port == 0) {
        std::fprintf(stderr, "端口不能为 0\n");
        return 2;
    }
    if (stCfg.heartbeatTimeoutSec <= 0) {
        std::fprintf(stderr, "心跳超时必须为正数\n");
        return 2;
    }

    dp::CServer stServer(stCfg);
    if (!stServer.start()) {
        LOG_ERROR("server start failed");
        return 1;
    }
    stServer.run();
    LOG_INFO("bye");
    return 0;
}
