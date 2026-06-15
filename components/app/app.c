#include "app.h"
#include <stddef.h>      /* NULL */

static const app_t *s_current_app = NULL;
static bool s_active = false;

void app_launch(const app_t *app)
{
    if (s_active && s_current_app && s_current_app->on_exit) {
        s_current_app->on_exit();
    }
    s_current_app = app;
    s_active = (app != NULL);
    if (s_active && app->on_enter) {
        app->on_enter();
    }
}

void app_exit(void)
{
    if (s_active && s_current_app && s_current_app->on_exit) {
        s_current_app->on_exit();
    }
    s_current_app = NULL;
    s_active = false;
}

bool app_is_active(void)
{
    return s_active;
}

const app_t *app_get_current(void)
{
    return s_current_app;
}

void app_render(void)
{
    if (s_active && s_current_app && s_current_app->on_render) {
        s_current_app->on_render();
    }
}

bool app_handle_key(uint8_t key_id, int event)
{
    if (s_active && s_current_app && s_current_app->on_key) {
        return s_current_app->on_key(key_id, event);
    }
    return false;
}
