package com.folotoy.passport.wallet.core;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;

public final class WalletEntries {
    private WalletEntries() {}
    public static List<WalletEntry> deduplicate(List<WalletEntry> input) {
        LinkedHashMap<String, WalletEntry> unique = new LinkedHashMap<>();
        for (WalletEntry entry : input) unique.putIfAbsent(entry.key(), entry);
        return new ArrayList<>(unique.values());
    }
}
