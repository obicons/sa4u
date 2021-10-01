#ifndef _instrument_noclash

#ifdef __cplusplus
extern "C"
#endif
void log_usage(unsigned, void *, unsigned long long size);

#define _instrument_noclash(varid, expr, instance_no)                                                                   \
    (*({                                                                                                                \
        typeof(expr) *_t_instrument_no_clash##instance_no = &(expr);                                                    \
        log_usage(varid, (void *) _t_instrument_no_clash##instance_no, sizeof(*_t_instrument_no_clash##instance_no));   \
        _t_instrument_no_clash##instance_no;                                                                            \
	}))
#endif

#include "hello.hpp"

#define a(x) ((x) * 10 + 1)

class my_class {
	int x, y;

public:
	int foo() {
		_instrument_noclash(1,(this->x=_instrument_noclash(0,(this->y=b),0)),1);
		return x;
	}

	int my_function() {
		_instrument_noclash(1,(this->x=10),2);
		return x;
	}
};

int main() {
	my_class foo;
	foo.foo();
}