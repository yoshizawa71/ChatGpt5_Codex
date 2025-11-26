#include "sdkconfig.h"
#include "energy_meter.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "modbus_rtu_master.h"
#include "modbus_guard_session.h"
#include "datalogger_driver.h"
#include "rs485_registry.h"  

// Contexto usado no iterate
typedef struct {
    esp_err_t last;
    int saved;
} iter_ctx_t;


extern int rs485_registry_adapter_link_anchor;
/*esp_err_t rs485_hint_get_used_fc(uint8_t addr, uint8_t *out_fc);
esp_err_t rs485_hint_set_used_fc(uint8_t addr, uint8_t used_fc);*/

static inline void _force_link_rs485_registry_adapter(void) {
    (void)rs485_registry_adapter_link_anchor;
}

/* ---------------------- GANCHOS do registry ----------------------
 * Implemente estes protótipos no seu registry (o “gerenciador” dos dados
 * lidos do config_driver.c). Estas versões weak permitem compilar hoje.
 */
// energy_meter.c — mude W para D nas WEAK
/*__attribute__((weak)) bool rs485_registry_get_channel_addr(uint8_t channel, uint8_t *out_addr)
{
    ESP_LOGD("ENERGY", "WEAK get_channel_addr() chamada (ch=%u) => sem cadastro", channel);
    (void)channel; (void)out_addr;
    return false;
}
__attribute__((weak)) int rs485_registry_get_channel_phase_count(uint8_t channel)
{
    ESP_LOGD("ENERGY", "WEAK get_channel_phase_count() chamada (ch=%u) => desconhecido", channel);
    (void)channel; return 0;
}
typedef bool (*rs485_iter_cb_t)(uint8_t channel, uint8_t addr, void *user);
__attribute__((weak)) int rs485_registry_iterate_configured(rs485_iter_cb_t cb, void *user)
{
    ESP_LOGD("ENERGY", "WEAK iterate_configured() chamada => retornando 0");
    (void)cb; (void)user; return 0;
}
*/

bool rs485_registry_get_channel_addr(uint8_t channel, uint8_t *out_addr);
int  rs485_registry_get_channel_phase_count(uint8_t channel);
int  rs485_registry_iterate_configured(bool (*cb)(uint8_t, uint8_t, void*), void *user);

/* ---------------------- SD: saída no formato desejado ----------------------
 * Nova rotina no sdcard_mmc.c escreverá: DATA | HORA | CANAL(string) | DADOS(valor)
 * Ex.: CANAL "3", "4.1", "4.2", "4.3"
 */
esp_err_t save_record_sd_rs485(int channel, int subindex, const char *value_str);

static const char *TAG = "ENERGY_METER";

/* JSY-MK-333 (correntes em 0x0103..0x0105; escala /100 A) */
#define JSY_REG_I_A   0x0103
#define JSY_I_SCALE   (100.0f)

/* ---------------------- Modbus helpers ---------------------- */
static inline esp_err_t read_currents_fc03(uint8_t addr, uint16_t raw[3])
{
    return modbus_master_read_holding_registers(addr, JSY_REG_I_A, 3, raw);
}
static inline esp_err_t read_currents_fc04(uint8_t addr, uint16_t raw[3])
{
    return modbus_master_read_input_registers(addr, JSY_REG_I_A, 3, raw);
}

esp_err_t energy_meter_init(void)
{
  /*  esp_err_t err = modbus_master_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "modbus_master_init() = %s", esp_err_to_name(err));
    return err;*/
    return ESP_OK;
}

static inline void convert_raw_currents(const uint16_t raw[3], float outI[3])
{
    outI[0] = (float)raw[0] / JSY_I_SCALE;
    outI[1] = (float)raw[1] / JSY_I_SCALE;
    outI[2] = (float)raw[2] / JSY_I_SCALE;
}

esp_err_t energy_meter_read_currents(uint8_t addr, float outI[3])
{
    if (!outI) return ESP_ERR_INVALID_ARG;

    #if !CONFIG_MODBUS_SERIAL_ENABLE
    // Quando Modbus estiver desativado (kill-switch), falhe explicitamente:
    return ESP_ERR_NOT_SUPPORTED;
#endif
    

     uint16_t raw[3] = {0};
    esp_err_t err = ESP_OK;

    // [NEW] Lê hint persistido para decidir a ordem de tentativa
    uint8_t hinted_fc = 0;
    (void)rs485_hint_get_used_fc(addr, &hinted_fc); // OK se não existir ainda

    // Janela curta e exclusiva para a transação Modbus
    MB_SESSION_WITH(pdMS_TO_TICKS(500)) {
        esp_err_t e1 = ESP_FAIL, e2 = ESP_FAIL;
        if (hinted_fc == 0x03) {
            e1 = read_currents_fc03(addr, raw);
            if (e1 != ESP_OK) e2 = read_currents_fc04(addr, raw);
            err = (e1 == ESP_OK) ? e1 : e2;
            if (e1 != ESP_OK && e2 == ESP_OK) {
                // Atualiza hint: agora 0x04 funcionou
                rs485_hint_set_used_fc(addr, 0x04);
            }
        } else if (hinted_fc == 0x04) {
            e1 = read_currents_fc04(addr, raw);
            if (e1 != ESP_OK) e2 = read_currents_fc03(addr, raw);
            err = (e1 == ESP_OK) ? e1 : e2;
            if (e1 != ESP_OK && e2 == ESP_OK) {
                // Atualiza hint: agora 0x03 funcionou
                rs485_hint_set_used_fc(addr, 0x03);
            }
        } else {
            // Sem hint: tenta 0x03 -> 0x04 e grava o que funcionou
            e1 = read_currents_fc03(addr, raw);
            if (e1 != ESP_OK) e2 = read_currents_fc04(addr, raw);
            err = (e1 == ESP_OK) ? e1 : e2;
            if (e1 == ESP_OK) {
                rs485_hint_set_used_fc(addr, 0x03);
            } else if (e2 == ESP_OK) {
                rs485_hint_set_used_fc(addr, 0x04);
            }
        }
    } // sessão fecha aqui, garantindo flush+idle
    
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "sem resposta do escravo addr=%u (timeout)", addr);
        return err;
    }
    if (err != ESP_OK) return err;

    convert_raw_currents(raw, outI);
    ESP_LOGI(TAG, "addr=%u  I: A=%.3f  B=%.3f  C=%.3f", addr, outI[0], outI[1], outI[2]);
    return ESP_OK;
    
}

/* Força salvar 3 linhas (3.1..3.3) */
esp_err_t energy_meter_save_currents(uint8_t channel, uint8_t addr)
{
    float I[3]; char buf[24];
    ESP_RETURN_ON_ERROR(energy_meter_read_currents(addr, I), TAG, "read");

    for (int sub = 1; sub <= 3; ++sub) {
        int n = snprintf(buf, sizeof(buf), "%.3f", I[sub - 1]);
        if (n <= 0 || n >= (int)sizeof(buf)) return ESP_FAIL;
        ESP_RETURN_ON_ERROR(save_record_sd_rs485(channel, sub, buf), TAG, "save");
    }
    return ESP_OK;
}

/* Por cadastro: 1 → grava "canal" (sem .1); 3 → grava "canal.1/.2/.3" */
esp_err_t energy_meter_save_currents_by_channel(uint8_t channel)
{
    uint8_t addr = 0;
    if (!rs485_registry_get_channel_addr(channel, &addr)) {
        ESP_LOGW("ENERGY", "[CH %u] não encontrado no cadastro.", channel);
        return ESP_ERR_NOT_FOUND;
    }

    float I[3] = {0};
    esp_err_t err = energy_meter_read_currents(addr, I);
    if (err != ESP_OK) {
        ESP_LOGW("ENERGY", "[CH %u] addr=%u leitura falhou: %s", channel, addr, esp_err_to_name(err));
        return err;
    }

    int phases = rs485_registry_get_channel_phase_count(channel);  // 1=mono, 3=tri
    if (phases != 1 && phases != 3) phases = 3; // default seguro

    ESP_LOGI("ENERGY", "[CH %u] addr=%u phases=%d  I_A=%.3f  I_B=%.3f  I_C=%.3f",
             channel, addr, phases, I[0], I[1], I[2]);

    char buf[24];
    if (phases == 1) {
        snprintf(buf, sizeof(buf), "%.3f", I[0]);
        return save_record_sd_rs485((int)channel, /*subindex=*/0, buf); // grava "3"
    } else {
        for (int sub = 1; sub <= 3; ++sub) {
            snprintf(buf, sizeof(buf), "%.3f", I[sub - 1]);
            esp_err_t e = save_record_sd_rs485((int)channel, sub, buf); // grava "4.1/4.2/4.3"
            if (e != ESP_OK) return e;
        }
        return ESP_OK;
    }
}

/* Opcional: salvar de todos os cadastrados */

static bool iter_cb(uint8_t channel, uint8_t addr, void *user)
{
	ESP_LOGI("ENERGY", "[iter] ch=%u addr=%u (vou salvar)", channel, addr);
    (void)addr;
    iter_ctx_t *ctx = (iter_ctx_t*)user;
    esp_err_t e = energy_meter_save_currents_by_channel(channel);
    if (e == ESP_OK) ctx->saved++; else ctx->last = e;
    return true;
}
esp_err_t energy_meter_save_registered_currents(void)
{
	_force_link_rs485_registry_adapter(); 
    ESP_LOGI("ENERGY", "TESTE:energy_meter_save_registered_currents(void)");

    // Mostra se estamos linkando para as funções reais ou as WEAK acima:
    ESP_LOGI("ENERGY", "hooks: get_addr=%p get_phase=%p iterate=%p",
             rs485_registry_get_channel_addr,
             rs485_registry_get_channel_phase_count,
             rs485_registry_iterate_configured);

    iter_ctx_t ctx = { .last = ESP_OK, .saved = 0 };
    int total = rs485_registry_iterate_configured(iter_cb, &ctx);

    ESP_LOGI("ENERGY", "iterate total=%d saved=%d last_err=%s",
             total, ctx.saved, esp_err_to_name(ctx.last ? ctx.last : ESP_OK));

    if (total <= 0) {
        ESP_LOGW("ENERGY", "Registry vazio ou WEAK iterate_configured em uso; nada a salvar.");
        return ESP_ERR_NOT_FOUND;
    }
    return (ctx.saved > 0) ? ESP_OK : (ctx.last ? ctx.last : ESP_FAIL);
}


esp_err_t energy_meter_read_all(uint8_t addr, energy_readings_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    float I[3];
    ESP_RETURN_ON_ERROR(energy_meter_read_currents(addr, I), TAG, "read");
    out->i_a = I[0]; out->i_b = I[1]; out->i_c = I[2];
    return ESP_OK;
}
