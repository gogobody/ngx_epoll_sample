# ngx_epoll_sample
this is a epoll event sample like ngx . its easy to use .

# run
`cmkae. && make`

then run`./eventModule` and `./testCli` to see how it works

# add your functions
before u do this , go to have a look at struct `task_t`
in dfs_task.h

1. then add `cmd` in dfs_task_cmd.h like `TASK_TEST`
2. go to `conn_listening_init` func in dn_conn_event to add your listen port
3. add a thread type in dn_thread.h like `THREAD_CLI_TEST`, add thread func to maintain epoll events,like `create_cli_thread`
4. add func to process events on the port, like `task_test()` \[task_test.h,task_test.cpp\] and add it to nn_rpc_server.cpp 

 

