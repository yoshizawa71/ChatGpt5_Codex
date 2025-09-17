/*
 * adaptive_delay.h
 *
 *  Created on: 16 de set. de 2025
 *      Author: geopo
 */

#ifndef SYSTEM_INCLUDE_ADAPTIVE_DELAY_H_
#define SYSTEM_INCLUDE_ADAPTIVE_DELAY_H_

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Estado leve para atraso adaptativo.
 *
 * Use um por tarefa/loop que queira regular. Cabe em .bss e não aloca heap.
 * Ideia: antes do “trabalho”, calcule um pequeno jitter (vTaskDelay).
 * Depois do “trabalho”, meça o custo real e alimente a média móvel.
 */
typedef struct {
    uint32_t ewma_cost_ms;   // Média móvel exponencial do custo (ms). Semente inicial.
    uint16_t min_ms;         // Limite inferior do atraso (ms)
    uint16_t max_ms;         // Limite superior do atraso (ms)
    uint8_t  salt_max_ms;    // Sal aleatório 0..salt_max_ms (ms) para desalinhavar bursts
    // Limiares de heap livre (em KiB) para penalidade suave (0 = desabilita)
    uint16_t heap_mid_kb;    // < mid => +15% de atraso
    uint16_t heap_low_kb;    // < low => +30% de atraso
} adaptive_delay_t;

/**
 * @brief Inicializa a estrutura com limites, semente e penalidades.
 *
 * @param ad            Ponteiro para o estado.
 * @param min_ms        Atraso mínimo (ms).
 * @param max_ms        Atraso máximo (ms).
 * @param seed_cost_ms  Semente da média (ex.: 150~250 ms).
 * @param salt_max_ms   Aleatório adicional 0..salt_max_ms (ms).
 * @param heap_mid_kb   Penalidade +15% se heap < mid KiB (0 = ignora).
 * @param heap_low_kb   Penalidade +30% se heap < low KiB (0 = ignora).
 */
void ad_init(adaptive_delay_t *ad,
             uint16_t min_ms, uint16_t max_ms,
             uint32_t seed_cost_ms,
             uint8_t salt_max_ms,
             uint16_t heap_mid_kb,
             uint16_t heap_low_kb);

/**
 * @brief  Chame imediatamente ANTES do trabalho.
 * @return Jitter (ms) sugerido para vTaskDelay() antes de começar.
 *
 * Também retorna t0_us (timestamp) para ad_after_work() calcular o custo.
 */
uint32_t ad_before_work(adaptive_delay_t *ad, uint64_t *t0_us_out);

/**
 * @brief  Chame DEPOIS do trabalho para atualizar a média com o custo medido.
 * @param  t0_us  Timestamp retornado por ad_before_work().
 */
void ad_after_work(adaptive_delay_t *ad, uint64_t t0_us);

#ifdef __cplusplus
}
#endif




#endif /* SYSTEM_INCLUDE_ADAPTIVE_DELAY_H_ */
