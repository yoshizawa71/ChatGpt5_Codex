#include "portal_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

// Flag global do portal; protegida por seção crítica.
static volatile bool s_portal_active = false;
static portMUX_TYPE s_portal_mux = portMUX_INITIALIZER_UNLOCKED;

void portal_state_set_active(bool on)
{
    portENTER_CRITICAL(&s_portal_mux);
    s_portal_active = on;
    portEXIT_CRITICAL(&s_portal_mux);
}

bool portal_state_is_active(void)
{
    portENTER_CRITICAL(&s_portal_mux);
    bool v = s_portal_active;
    portEXIT_CRITICAL(&s_portal_mux);
    return v;
}

// Implementação "forte" do gancho global.
// Outros módulos que só conhecem factory_portal_active()
// continuam funcionando sem incluir o header.
bool factory_portal_active(void)
{
    return portal_state_is_active();
}
