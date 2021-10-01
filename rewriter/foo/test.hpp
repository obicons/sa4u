#ifndef _instrument_noclash
#define _instrument_noclash(name, expr)                                     \
    (*({                                                                    \
        typeof(expr) *_t_instrument_no_clash = &(expr);                     \
        _t_instrument_no_clash;                                             \
	}))
#endif

#pragma once

struct test {
    int x;

    void my_func() {
        _instrument_noclash("test::x", (this->x = 40));
    }
};
