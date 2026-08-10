/* signals.h — KratosOS Init Signal Handling Module Header */

#ifndef KRATOS_INIT_SIGNALS_H
#define KRATOS_INIT_SIGNALS_H

#include "init.h"

void setup_signal_handlers(void);
void reap_zombies(void);

#endif /* KRATOS_INIT_SIGNALS_H */
