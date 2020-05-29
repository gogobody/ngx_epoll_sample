#include "dn_error_log.h"
#include "dn_conf.h"
#include "dn_time.h"

int dn_error_log_init(cycle_t *cycle)
{
    log_t         *slog = nullptr;
    conf_server_t *sconf = nullptr;
    
    errno = 0;

    sconf = (conf_server_t *)cycle->sconf;
    slog = cycle->error_log;
    
    slog->file->name = sconf->error_log;
    slog->log_level = sconf->log_level;
    
    error_log_init(slog, (log_time_ptr)time_logstr, nullptr);
    
    return NGX_OK;
}

int dn_error_log_release(cycle_t *cycle)
{
    return error_log_release(cycle->error_log);
}

