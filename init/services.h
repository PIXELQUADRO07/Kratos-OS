/* services.h — KratosOS Init System Services & Hostname Module Header */

#ifndef KRATOS_INIT_SERVICES_H
#define KRATOS_INIT_SERVICES_H

#include "init.h"

void set_hostname(void);
void run_sysinit(void);
void run_services(void);

#endif /* KRATOS_INIT_SERVICES_H */
