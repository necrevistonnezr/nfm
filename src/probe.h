#ifndef NFM_PROBE_H
#define NFM_PROBE_H
#include "nfm.h"
int   probe_file(const char *path, const Capabilities *caps, ProbeResult *result);
void  format_duration(double seconds, char *buf, int buflen);
void  format_size(long long bytes, char *buf, int buflen);
void  format_bitrate(long long bps, char *buf, int buflen);
float parse_fps_str(const char *fps_str);
#endif
