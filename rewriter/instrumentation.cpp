#ifndef _instrument_noclash

#ifdef __cplusplus
extern "C"
#endif
void log_usage(int, unsigned, void *, unsigned long long size);

#define _instrument_noclash(vartype, varid, expr, instance_no)                                                                   \
    (*({                                                                                                                         \
        typeof(expr) *_t_instrument_no_clash##instance_no = &(expr);                                                             \
        log_usage(vartype, varid, (void *) _t_instrument_no_clash##instance_no, sizeof(*_t_instrument_no_clash##instance_no));   \
        _t_instrument_no_clash##instance_no;                                                                                     \
	}))
#endif
