#ifndef NFM_PRESETS_H
#define NFM_PRESETS_H
#include "nfm.h"
int   load_presets(Preset **out, int *count_out, const Capabilities *caps);
void  free_presets(Preset *presets);
int   init_user_preset_dir(void);
float estimate_savings(const ProbeResult *probe, const char *target_args);
void  get_user_preset_dir(char *buf, size_t buflen);
#endif
