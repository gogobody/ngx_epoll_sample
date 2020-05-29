//
// Created by ginux on 2020/5/29.
//

#include "task_test.h"
#include "nn_task_queue.h"
#include "nn_net_response_handler.h"

int task_test(task_t *task) {
    task_queue_node_t *node = queue_data(task, task_queue_node_t, tk);
    printf("ls key:%s\n", task->key);

    // just for example

    if(strlen(task->key)==0 ){
        task->ret = KEY_NOTEXIST;

        // if u not use malloc then make task->data  to null
        task->data = nullptr;
        task->data_len = 0;
        return write_back(node); // 这里是 cli thread ，直接交给cli thread 处理（直接发送回去）
    }
    // do some task deal here

    //
    task->ret = NGX_OK;
    task->data = nullptr;
    task->data_len = 0;
    return write_back(node);

}
