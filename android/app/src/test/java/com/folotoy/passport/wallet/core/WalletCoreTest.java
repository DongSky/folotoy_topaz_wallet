package com.folotoy.passport.wallet.core;

import static org.junit.Assert.*;

import java.math.BigDecimal;
import java.time.Instant;
import java.util.List;
import org.junit.Test;

public class WalletCoreTest {
    @Test public void addressValidationIsNetworkAware() {
        assertTrue(AddressValidator.isValid(Network.ETHEREUM, "0x52908400098527886E0F7030069857D2E4169EE7"));
        assertFalse(AddressValidator.isValid(Network.ETHEREUM, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kygt080"));
        assertTrue(AddressValidator.isValid(Network.BITCOIN, "1BoatSLRHtKNngkdXEeobR76b53LETtpyT"));
        assertTrue(AddressValidator.isValid(Network.BITCOIN, "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh"));
        assertFalse(AddressValidator.isValid(Network.BITCOIN, "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wla"));
        assertTrue(AddressValidator.isValid(Network.SOLANA, "11111111111111111111111111111111"));
        assertFalse(AddressValidator.isValid(Network.SOLANA, "0OIl"));
    }

    @Test public void registryIdentifiersAreExplicitAndStructurallyValid() {
        assertEquals(8, Network.values().length);
        assertFalse(AssetRegistry.supports(Network.BASE, "USDT"));
        for (AssetRegistry.Asset a : AssetRegistry.all()) {
            assertTrue(a.sourceUrl().startsWith("https://"));
            if (a.contractOrMint() != null && a.network().evm()) assertTrue(a.contractOrMint().matches("0x[0-9a-fA-F]{40}"));
            if (a.contractOrMint() != null && a.network() == Network.SOLANA)
                assertTrue(AddressValidator.isValid(Network.SOLANA, a.contractOrMint()));
        }
    }

    @Test public void dedupUsesNetworkAddressAndAsset() {
        WalletEntry a = new WalletEntry(Network.ETHEREUM, "0x52908400098527886E0F7030069857D2E4169EE7", "ETH", "A");
        WalletEntry same = new WalletEntry(Network.ETHEREUM, "0x52908400098527886e0f7030069857d2e4169ee7", "ETH", "B");
        WalletEntry differentAsset = new WalletEntry(Network.ETHEREUM, a.address(), "USDC", "C");
        assertEquals(List.of(a, differentAsset), WalletEntries.deduplicate(List.of(a, same, differentAsset)));
    }

    @Test public void bech32CaseDoesNotDoubleCountBitcoin() {
        String address="bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh";
        WalletEntry lower=new WalletEntry(Network.BITCOIN,address,"BTC","lower");
        WalletEntry upper=new WalletEntry(Network.BITCOIN,address.toUpperCase(java.util.Locale.ROOT),"BTC","upper");
        assertEquals(lower.key(),upper.key());
        assertEquals(1,WalletEntries.deduplicate(List.of(lower,upper)).size());
    }

    @Test public void giantBase58InputsAreRejectedBeforeDecoding() {
        assertFalse(AddressValidator.isValid(Network.BITCOIN,"1".repeat(10000)));
        assertFalse(AddressValidator.isValid(Network.SOLANA,"1".repeat(10000)));
    }

    @Test public void totalsAreExactUniqueAndIncompleteWhenMissing() {
        Instant now = Instant.parse("2026-09-22T10:00:00Z");
        WalletEntry eth = new WalletEntry(Network.ETHEREUM, "0x52908400098527886E0F7030069857D2E4169EE7", "ETH", "Eth");
        WalletEntry duplicate = new WalletEntry(Network.ETHEREUM, eth.address().toLowerCase(), "ETH", "Duplicate");
        WalletEntry usdc = new WalletEntry(Network.BASE, "0x52908400098527886E0F7030069857D2E4169EE7", "USDC", "Base USDC");
        CachedAmount ethBalance = CachedAmount.success(new BigDecimal("0.123456789123456789"), now.minusSeconds(60));
        CachedAmount ethPrice = CachedAmount.success(new BigDecimal("3123.456789"), now.minusSeconds(30));
        Valuation.Result result = Valuation.total(List.of(eth, duplicate, usdc),
                java.util.Map.of(eth.key(), ethBalance),
                java.util.Map.of(eth.priceKey("USD"), ethPrice), "USD", now, 300);
        assertEquals(new BigDecimal("385.611946135802466750190521"), result.total());
        assertTrue(result.incomplete());
        assertEquals(1, result.includedEntries());
    }

    @Test public void staleValuesRemainButMarkTotalIncomplete() {
        Instant now = Instant.parse("2026-09-22T10:00:00Z");
        WalletEntry btc = new WalletEntry(Network.BITCOIN, "1BoatSLRHtKNngkdXEeobR76b53LETtpyT", "BTC", "BTC");
        Valuation.Result result = Valuation.total(List.of(btc),
                java.util.Map.of(btc.key(), CachedAmount.success(new BigDecimal("2"), now.minusSeconds(1000))),
                java.util.Map.of(btc.priceKey("CNY"), CachedAmount.success(new BigDecimal("500000"), now)),
                "CNY", now, 300);
        assertEquals(new BigDecimal("1000000"), result.total());
        assertTrue(result.incomplete());
    }
}
