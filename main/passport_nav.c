#include "passport_ui.h"

uint8_t passport_nav_kind(const passport_nav_t *nav)
{
    return nav->category;
}
void passport_nav_hide(passport_nav_t *nav) { nav->revealed = false; }
void passport_nav_key(passport_nav_t *nav, wallet_key_t key,
                       const uint16_t counts[WALLET_KIND_COUNT])
{
    if (key == WALLET_KEY_BACK) {
        nav->revealed = false;
        nav->page = 0;
        nav->screen = nav->screen == PASSPORT_PAGE ? PASSPORT_CARD_MENU : PASSPORT_HOME;
        return;
    }
    switch (nav->screen) {
    case PASSPORT_HOME:
        if (key == WALLET_KEY_UP || key == WALLET_KEY_DOWN) nav->home_selection ^= 1;
        else if (key == WALLET_KEY_OK) {
            nav->screen = nav->home_selection ? PASSPORT_IMAGE : PASSPORT_CARD_MENU;
            nav->category = 0;
        }
        break;
    case PASSPORT_CARD_MENU:
        if (key == WALLET_KEY_UP) nav->category = (nav->category + 4) % 5;
        else if (key == WALLET_KEY_DOWN) nav->category = (nav->category + 1) % 5;
        else if (key == WALLET_KEY_OK) {
            nav->screen = PASSPORT_PAGE;
            nav->page = 0;
            nav->revealed = false;
        }
        break;
    case PASSPORT_PAGE: {
        uint16_t count = counts[passport_nav_kind(nav)];
        if (key == WALLET_KEY_UP && count) nav->page = (nav->page + count - 1) % count;
        else if (key == WALLET_KEY_DOWN && count) nav->page = (nav->page + 1) % count;
        else if (key == WALLET_KEY_OK) nav->revealed = !nav->revealed;
        break;
    }
    case PASSPORT_IMAGE:
        break;
    }
}
