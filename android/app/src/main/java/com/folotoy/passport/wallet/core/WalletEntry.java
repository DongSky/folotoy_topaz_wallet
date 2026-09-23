package com.folotoy.passport.wallet.core;

import java.util.Locale;

public record WalletEntry(Network network, String address, String asset, String label) {
    public WalletEntry {
        if (network == null || address == null || asset == null || label == null) throw new IllegalArgumentException("Entry fields required");
        if (!AddressValidator.isValid(network, address)) throw new IllegalArgumentException("Invalid " + network.label() + " address");
        // BIP173 permits all-uppercase encoding of the same witness program.
        // Canonicalize it for deduplication, requests, and receiving-page grouping.
        if (network == Network.BITCOIN && address.toLowerCase(Locale.ROOT).startsWith("bc1")) {
            address = address.toLowerCase(Locale.ROOT);
        }
        if (!AssetRegistry.supports(network, asset)) throw new IllegalArgumentException("Unregistered asset " + asset + " on " + network.label());
        if (label.length() > 80) throw new IllegalArgumentException("Label is longer than 80 characters");
    }
    public String key() {
        String a = network.evm() ? address.toLowerCase(Locale.ROOT) : address;
        return network.id() + ":" + a + ":" + asset.toUpperCase(Locale.ROOT);
    }
    public String priceKey(String currency){return "PRICE:"+network.id()+":"+asset.toUpperCase(Locale.ROOT)+":"+currency.toUpperCase(Locale.ROOT);}
}
