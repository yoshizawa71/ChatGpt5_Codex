/*
 * adaptive_delay.c
 *
 *  Created on: 16 de set. de 2025
 *      Author: geopo
 */

#include "adaptive_delay.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <stdint.h>

static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi){
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void ad_init(adaptive_delay_t *ad,
             uint16_t min_ms, uint16_t max_ms,
             uint32_t seed_cost_ms,
             uint8_t salt_max_ms,
             uint16_t heap_mid_kb,
             uint16_t heap_low_kb)
{
    if (!ad) return;
    if (max_ms < min_ms) max_ms = min_ms;

    ad->ewma_cost_ms = seed_cost_ms ? seed_cost_ms : min_ms;
    ad->min_ms       = min_ms;
    ad->max_ms       = max_ms;
    ad->salt_max_ms  = salt_max_ms;
    ad->heap_mid_kb  = heap_mid_kb;
    ad->heap_low_kb  = heap_low_kb;
}

uint32_t ad_before_work(adaptive_delay_t *ad, uint64_t *t0_us_out)
{
    if (t0_us_out) *t0_us_out = esp_timer_get_time();

    // === 1) Carga baseada no custo médio (100..600ms -> 0..65535) ===
    uint32_t avg = ad ? ad->ewma_cost_ms : 0;
    uint32_t load655 = 0;
    if (avg <= 100) {
        load655 = 0;
    } else if (avg >= 600) {
        load655 = 65535U;
    } else {
        // (avg - 100) * 65535 / 500
        load655 = (uint32_t)(((uint64_t)(avg - 100) * 65535U) / 500U);
    }

    // === 2) Penalidade por pouca heap livre (0, +15%, +30%) ===
    const uint32_t PENAL_15 = 9830U;   // ~0,15 * 65535
    const uint32_t PENAL_30 = 19661U;  // ~0,30 * 65535
    uint32_t pen655 = 0;
    if (ad && (ad->heap_low_kb || ad->heap_mid_kb)) {
        size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        if (ad->heap_low_kb && free8 < (size_t)ad->heap_low_kb * 1024U) {
            pen655 = PENAL_30;
        } else if (ad->heap_mid_kb && free8 < (size_t)ad->heap_mid_kb * 1024U) {
            pen655 = PENAL_15;
        }
    }

    uint32_t score655 = load655 + pen655;
    if (score655 > 65535U) score655 = 65535U;

    // === 3) Mapeia score para [min..max] e adiciona "sal" aleatório ===
    uint32_t span = (ad && ad->max_ms > ad->min_ms) ? (ad->max_ms - ad->min_ms) : 0;
    uint32_t base = (ad ? ad->min_ms : 0) + (uint32_t)(((uint64_t)span * score655) >> 16);

    uint32_t salt = (ad && ad->salt_max_ms) ? (esp_random() % (ad->salt_max_ms + 1U)) : 0;

    return base + salt; // jitter em ms
}

void ad_after_work(adaptive_delay_t *ad, uint64_t t0_us)
{
    if (!ad) return;

    uint64_t t1_us  = esp_timer_get_time();
    uint32_t cost_ms = (uint32_t)((t1_us - t0_us) / 1000ULL);

    // EWMA extremamente barata: alpha = 1/4  =>  avg += (cost - avg) >> 2
    int32_t delta = (int32_t)cost_ms - (int32_t)ad->ewma_cost_ms;
    ad->ewma_cost_ms = (uint32_t)((int32_t)ad->ewma_cost_ms + (delta >> 2));

    // Garante que a média fique na janela [min..max]
    ad->ewma_cost_ms = clamp_u32(ad->ewma_cost_ms, ad->min_ms, ad->max_ms);
}



