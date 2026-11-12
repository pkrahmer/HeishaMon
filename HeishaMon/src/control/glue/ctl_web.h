/*
 * The control page of HeishaMon's web interface: /control shows the loop's last decision and a
 * form with every setting, and /savecontrol applies the form the way a setting topic does. One
 * hook line at the start of upstream's web server callback hands it every step of a request; it
 * takes the steps of its own two routes and leaves every other request to upstream untouched.
 */
#ifndef CTL_WEB_H
#define CTL_WEB_H

#include <stdint.h>

struct webserver_t;

/* Returns false, touching nothing, for every step of a request that is neither /control nor
 * /savecontrol. For a step of those two it handles the step, writes the result upstream's
 * callback would return into *result (-1 refuses a save whose argument buffer cannot be
 * allocated) and returns true. */
bool ctl_web(struct webserver_t *client, void *dat, int8_t *result);

#endif
