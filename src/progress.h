#ifndef NFM_PROGRESS_H
#define NFM_PROGRESS_H
#include "nfm.h"
int run_encoding(AppCtx *ctx, const char *input, const char *extra_args,
                 const char *output, double total_duration);
#endif
