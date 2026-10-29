#include "config.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "zql";
    string passwd = "030929";
    string databasename = "yourdb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    //创建 WebServer服务器对象
    WebServer server;

    //初始化保存服务器配置
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    

    //初始化日志系统
    server.log_write();

    //初始化数据库连接池
    server.sql_pool();

    //初始化线程池
    server.thread_pool();

    //设置 ET/LT 触发模式
    server.trig_mode();

    //创建 socket、绑定端口、开始监听
    if (!server.eventListen())
    {
        fprintf(stderr, "===========Server start failed===========\n");
        fprintf(stderr, "端口 %d 可能已被占用，或当前用户没有绑定权限。\n", config.PORT);
        fprintf(stderr, "换个端口重试：./server -p 9090\n");
        return 1;
    }

    //启动成功提示（stdout 默认带缓冲，重定向到文件时需 fflush 才能立刻看到）
    printf("===========Server is running===========\n");
    printf("===========Listening on port %d===========\n", config.PORT);
    fflush(stdout);

    //进入事件循环，开始处理客户端请求
    server.eventLoop();

    return 0;
}