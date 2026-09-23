package com.folotoy.passport.wallet.core;

public enum Network {
    BITCOIN("bitcoin", "Bitcoin", "BTC", false),
    ETHEREUM("ethereum", "Ethereum", "ETH", true),
    ARBITRUM("arbitrum-one", "Arbitrum One", "ETH", true),
    SOLANA("solana", "Solana", "SOL", false),
    BSC("bsc", "BNB Smart Chain", "BNB", true),
    BASE("base", "Base", "ETH", true),
    OPTIMISM("op-mainnet", "OP Mainnet", "ETH", true),
    POLYGON("polygon-pos", "Polygon PoS", null, true);

    private final String id, label, nativeAsset;
    private final boolean evm;
    Network(String id, String label, String nativeAsset, boolean evm) {
        this.id = id; this.label = label; this.nativeAsset = nativeAsset; this.evm = evm;
    }
    public String id() { return id; }
    public String label() { return label; }
    public String nativeAsset() { return nativeAsset; }
    public boolean evm() { return evm; }
    public static Network fromId(String id) {
        for (Network n : values()) if (n.id.equals(id)) return n;
        throw new IllegalArgumentException("Unsupported network: " + id);
    }
}
