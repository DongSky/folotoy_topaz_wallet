#include "passport_ui.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const uint16_t counts[5] = {1, 2, 2, 3, 4};
    passport_nav_t n = {0};
    passport_nav_key(&n, WALLET_KEY_DOWN, counts);
    passport_nav_key(&n, WALLET_KEY_OK, counts);
    assert(n.screen == PASSPORT_IMAGE);
    passport_nav_key(&n, WALLET_KEY_BACK, counts);
    assert(n.screen == PASSPORT_HOME && n.home_selection == 1);
    passport_nav_key(&n, WALLET_KEY_UP, counts);
    passport_nav_key(&n, WALLET_KEY_OK, counts);
    assert(n.screen == PASSPORT_CARD_MENU);
    passport_nav_key(&n, WALLET_KEY_UP, counts);
    assert(n.category == 4 && passport_nav_kind(&n) == 4);
    passport_nav_key(&n, WALLET_KEY_OK, counts);
    assert(n.screen == PASSPORT_PAGE && !n.revealed);
    passport_nav_key(&n, WALLET_KEY_UP, counts);
    assert(n.page == 3);
    passport_nav_key(&n, WALLET_KEY_OK, counts);
    assert(n.revealed);
    passport_nav_hide(&n);
    assert(!n.revealed);
    passport_nav_key(&n, WALLET_KEY_BACK, counts);
    assert(n.screen == PASSPORT_CARD_MENU && n.page == 0);
    passport_nav_key(&n, WALLET_KEY_BACK, counts);
    assert(n.screen == PASSPORT_HOME);
    const uint16_t empty[5] = {0};
    n = (passport_nav_t){.screen = PASSPORT_PAGE};
    passport_nav_key(&n, WALLET_KEY_UP, empty);
    passport_nav_key(&n, WALLET_KEY_DOWN, empty);
    assert(n.page == 0);
    puts("Passport home/Card/Image navigation: PASS");
}
