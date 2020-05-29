#include <iostream>
#include "core/dfs_types.h"
#include "dn_cycle.h"
#include "dn_process.h"
#include "dn_conf.h"
#include "dn_time.h"
#include "dn_module.h"
string_t     config_file;
#define PATH_SIZE 256
int main(int argc, char **argv) {
    cycle_t       *cycle = nullptr;

    char path[PATH_SIZE];

    cycle = dn_cycle_create(); //创建内存池
    time_init();//时间缓存

    umask(0022);//默认创建新文件权限为755

    // 配置文件
    if(!getcwd(path,PATH_SIZE)){
        printf("get failed!");
        return 0;
    }

    strcat(path,"/../config.conf");

    config_file.data = (uchar_t *)strndup(path,
                       strlen(path));
    config_file.len = strlen(path);
    //


    // cycle init 主要初始化配置文件结构体，解析配置文件，初始化error log相关结构体
    if ((dn_cycle_init(cycle)) != NGX_OK)
    {
        fprintf(stderr, "dn_cycle_init fail\n");

        goto out;
    }
    // init module index
    dfs_module_setup();

    if ((dfs_module_master_init(cycle)) != NGX_OK)  // init master 函数
    {
        fprintf(stderr, "master init fail\n");

        goto out;
    }


    process_master_cycle(cycle, argc, argv);

out:

    return 0;
}
