#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local gc_sections="-Wl,--gc-sections"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        gc_sections="-Wl,-dead_strip"
    fi

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wallet_core.c main/wallet_core.c \
        -o "${test_dir}/test_wallet_core"
    "${test_dir}/test_wallet_core"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_nav.c main/passport_nav.c -o "${test_dir}/test_passport_nav"
    "${test_dir}/test_passport_nav"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_jpeg.c main/passport_jpeg.c -o "${test_dir}/test_passport_jpeg"
    "${test_dir}/test_passport_jpeg"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/fakes/passport_capture -Imain \
        tests/test_passport_capture.c main/passport_capture.c -o "${test_dir}/test_passport_capture"
    "${test_dir}/test_passport_capture"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wallet_transfer.c main/wallet_transfer.c main/wallet_core.c \
        -o "${test_dir}/test_wallet_transfer"
    "${test_dir}/test_wallet_transfer"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DWALLET_STORE_HOST_TEST -Imain \
        tests/test_wallet_store.c main/wallet_store.c main/wallet_core.c \
        -o "${test_dir}/test_wallet_store"
    "${test_dir}/test_wallet_store"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DWALLET_BLE_HOST_TEST -Imain \
        tests/test_wallet_ble.c main/wallet_ble.c \
        -o "${test_dir}/test_wallet_ble"
    "${test_dir}/test_wallet_ble"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/fakes/wallet_ble -Imain \
        tests/test_wallet_ble_runtime.c main/wallet_transfer.c main/wallet_core.c \
        -o "${test_dir}/test_wallet_ble_runtime"
    "${test_dir}/test_wallet_ble_runtime"
    for stock_module in protocol transfer screenshot; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
            "tests/test_stock_${stock_module}.c" "main/stock_${stock_module}.c" \
            -o "${test_dir}/test_stock_${stock_module}"
        "${test_dir}/test_stock_${stock_module}"
    done
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_stock_screenshot_session.c main/stock_screenshot_session.c main/stock_screenshot.c \
        -o "${test_dir}/test_stock_screenshot_session"
    "${test_dir}/test_stock_screenshot_session"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_stock_rtttl.c main/stock_rtttl.c -o "${test_dir}/test_stock_rtttl"
    "${test_dir}/test_stock_rtttl"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/fakes/stock_music \
        -Itests/fakes/stock_profile -Imain tests/test_stock_music.c main/stock_music.c \
        main/stock_rtttl.c main/stock_transfer.c -o "${test_dir}/test_stock_music"
    "${test_dir}/test_stock_music"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/fakes/stock_identity -Imain \
        tests/test_stock_identity.c main/stock_identity.c \
        -o "${test_dir}/test_stock_identity"
    "${test_dir}/test_stock_identity"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_stock_profile.c main/stock_profile.c \
        -o "${test_dir}/test_stock_profile"
    "${test_dir}/test_stock_profile"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/fakes/stock_profile -Imain \
        tests/test_stock_profile_nvs.c main/stock_profile.c main/stock_profile_nvs.c \
        -o "${test_dir}/test_stock_profile_nvs"
    "${test_dir}/test_stock_profile_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/fakes/stock_ble -Imain \
        tests/test_stock_ble.c main/stock_ble.c main/stock_protocol.c \
        -o "${test_dir}/test_stock_ble"
    "${test_dir}/test_stock_ble"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DSTOCK_AVATAR_HOST_TEST \
        -Itests/fakes/stock_avatar -Imain tests/test_stock_avatar.c main/stock_avatar.c \
        -lz -o "${test_dir}/test_stock_avatar"
    "${test_dir}/test_stock_avatar"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${gc_sections}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_card_font.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir
    local cjson_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    # JSON behavior uses the exact bundled IDF cJSON. Keep the static gate
    # dependency-free; this focused host test is mandatory in the firmware gate.
    cjson_dir="${CJSON_DIR:-${IDF_PATH:-}/components/json/cJSON}"
    if [[ ! -f "${cjson_dir}/cJSON.c" || ! -f "${cjson_dir}/cJSON.h" ]]; then
        echo "ERROR: stock profile JSON tests require IDF cJSON (or CJSON_DIR)." >&2
        return 1
    fi
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations \
        -c "${cjson_dir}/cJSON.c" -o "${validation_build_dir}/profile_cjson.o"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -I"${cjson_dir}" \
        tests/test_stock_profile_json.c main/stock_profile.c main/stock_profile_json.c \
        "${validation_build_dir}/profile_cjson.o" -lm \
        -o "${validation_build_dir}/test_stock_profile_json"
    "${validation_build_dir}/test_stock_profile_json"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/fakes/stock_content -Imain -I"${cjson_dir}" \
        tests/test_stock_content.c main/stock_content.c main/stock_transfer.c \
        main/stock_profile.c main/stock_profile_json.c \
        "${validation_build_dir}/profile_cjson.o" -lm \
        -o "${validation_build_dir}/test_stock_content"
    "${validation_build_dir}/test_stock_content"

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
