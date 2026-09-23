package com.folotoy.passport.wallet.core;

import java.util.ArrayList;
import java.util.List;

public final class AssetRegistry {
    public record Asset(Network network, String symbol, int decimals, String contractOrMint,
                        String origin, String priceId, String sourceUrl) {}
    private static final String CIRCLE = "https://developers.circle.com/stablecoins/usdc-contract-addresses";
    private static final String TETHER = "https://tether.to/en/supported-protocols/";
    private static final List<Asset> ASSETS = List.of(
        new Asset(Network.BITCOIN,"BTC",8,null,"native","bitcoin","https://developer.bitcoin.org/reference/"),
        new Asset(Network.ETHEREUM,"ETH",18,null,"native","ethereum","https://ethereum.org/en/developers/docs/intro-to-ethereum/"),
        new Asset(Network.ETHEREUM,"USDC",6,"0xA0b86991c6218b36c1d19d4a2e9eb0ce3606eb48","native","usd-coin",CIRCLE),
        new Asset(Network.ETHEREUM,"USDT",6,"0xdAC17F958D2ee523a2206206994597C13D831ec7","native","tether",TETHER),
        new Asset(Network.ARBITRUM,"ETH",18,null,"native","ethereum","https://docs.arbitrum.io/"),
        new Asset(Network.ARBITRUM,"USDC",6,"0xaf88d065e77c8cC2239327C5EDb3A432268e5831","native","usd-coin",CIRCLE),
        new Asset(Network.ARBITRUM,"USDT0",6,"0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9","USDT0","usdt0","https://docs.usdt0.to/technical-documentation/deployments"),
        new Asset(Network.SOLANA,"SOL",9,null,"native","solana","https://solana.com/docs"),
        new Asset(Network.SOLANA,"USDC",6,"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","native","usd-coin",CIRCLE),
        new Asset(Network.SOLANA,"USDT",6,"Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB","native","tether",TETHER),
        new Asset(Network.BSC,"BNB",18,null,"native","binancecoin","https://docs.bnbchain.org/"),
        new Asset(Network.BSC,"USDC",18,"0x8AC76a51cc950d9822D68b83fE1Ad97B32Cd580d","Binance-Peg; USDC market proxy","usd-coin","https://bscscan.com/token/0x8ac76a51cc950d9822d68b83fe1ad97b32cd580d"),
        new Asset(Network.BSC,"USDT",18,"0x55d398326f99059fF775485246999027B3197955","Binance-Peg; Tether market proxy","tether","https://bscscan.com/token/0x55d398326f99059ff775485246999027b3197955"),
        new Asset(Network.BASE,"ETH",18,null,"native","ethereum","https://docs.base.org/"),
        new Asset(Network.BASE,"USDC",6,"0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913","native","usd-coin",CIRCLE),
        new Asset(Network.OPTIMISM,"ETH",18,null,"native","ethereum","https://docs.optimism.io/"),
        new Asset(Network.OPTIMISM,"USDC",6,"0x0b2C639c533813f4Aa9D7837CAf62653d097Ff85","native","usd-coin",CIRCLE),
        new Asset(Network.OPTIMISM,"USDT0",6,"0x01bFF41798a0BcF287b996046Ca68b395DbC1071","USDT0","usdt0","https://docs.usdt0.to/technical-documentation/deployments"),
        new Asset(Network.POLYGON,"USDC",6,"0x3c499c542cEF5E3811e1192ce70d8cC03d5c3359","native","usd-coin",CIRCLE),
        new Asset(Network.POLYGON,"USDT0",6,"0xc2132D05D31c914a87C6611C10748AEb04B58e8F","USDT0 migration","usdt0","https://docs.usdt0.to/technical-documentation/deployments")
    );
    private AssetRegistry() {}
    public static List<Asset> all() { return ASSETS; }
    public static List<Asset> forNetwork(Network n) { List<Asset> out=new ArrayList<>(); for(Asset a:ASSETS) if(a.network()==n) out.add(a); return out; }
    public static Asset get(Network n, String symbol) { for(Asset a:ASSETS) if(a.network()==n && a.symbol().equalsIgnoreCase(symbol)) return a; throw new IllegalArgumentException("Asset not registered"); }
    public static boolean supports(Network n, String symbol) { try { get(n,symbol); return true; } catch(IllegalArgumentException e) { return false; } }
}
