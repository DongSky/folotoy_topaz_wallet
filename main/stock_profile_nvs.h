#pragma once
#include "stock_profile.h"

#define STOCK_PROFILE_NAMESPACE "trae_cfg"
#define STOCK_PROFILE_KEY "profile"
typedef enum {
    STOCK_PROFILE_STORE_OK = 0,
    STOCK_PROFILE_STORE_INVALID_ARG,
    STOCK_PROFILE_STORE_NOT_FOUND,
    STOCK_PROFILE_STORE_INVALID_DATA,
    STOCK_PROFILE_STORE_IO_ERROR,
    STOCK_PROFILE_STORE_CANCELLED
} stock_profile_store_result_t;

/* Blocking worker/init-only operations. Caller serializes access. Initialize
 * default NVS without explicit partition erase or erase/retry recovery. IDF
 * initialization can perform its own NVS page recovery: this is NOT a forensic
 * or physically read-only adapter. Only default nvs / trae_cfg / profile is
 * addressed; never access cardid or other namespaces. Load uses READONLY and
 * leaves output untouched on any error; migrated is optional and cleared on
 * error. Unlike stock startup, successful legacy migration is IN MEMORY ONLY.
 * Caller explicitly saves after deciding to persist. Save writes the245-byte
 * current-version blob and commits. No logging or credential fields. */
stock_profile_store_result_t stock_profile_nvs_load(stock_profile_t *output,
                                                    bool *migrated);
stock_profile_store_result_t stock_profile_nvs_save(const stock_profile_t *profile);

/* Optional cancellation guard, called synchronously before init/open/set/commit
 * and after blocking stages. False returns CANCELLED and prevents subsequent
 * operations. Guard/context remain caller-owned until return. NULL permits all.
 * The last successful guard before EACH storage API is its authorization point;
 * cancellation after that point cannot undo that operation. In particular,
 * nvs_set_blob can itself write durable flash before nvs_commit. CANCELLED after
 * set/commit does NOT promise rollback; callers suppress stale events and may
 * reload persisted state later. No erase/retry or compensating writes occur. */
typedef bool (*stock_profile_save_guard_t)(void *context);
stock_profile_store_result_t stock_profile_nvs_save_guarded(const stock_profile_t *profile,
    stock_profile_save_guard_t guard, void *context);
