package com.folotoy.passport.wallet.provider;

import java.math.BigDecimal;
import java.math.BigInteger;

public final class ProviderValues {
    private ProviderValues() {}
    public static BigDecimal evmQuantity(String hex, int decimals) {
        if (hex == null || !hex.matches("0x[0-9a-fA-F]+")) throw new IllegalArgumentException("Invalid EVM quantity");
        return new BigDecimal(new BigInteger(hex.substring(2), 16)).movePointLeft(decimals).stripTrailingZeros();
    }
    public static BigDecimal integerUnits(String integer, int decimals) {
        if (integer == null || !integer.matches("[0-9]+")) throw new IllegalArgumentException("Invalid chain quantity");
        return new BigDecimal(new BigInteger(integer)).movePointLeft(decimals).stripTrailingZeros();
    }
}
