package com.folotoy.passport.wallet.provider;

import static org.junit.Assert.*;
import java.math.BigDecimal;
import org.junit.Test;
import com.folotoy.passport.wallet.core.*;
import java.util.List;
import org.json.JSONObject;

public class ProviderParsingTest {
    @Test public void evmHexUsesAssetDecimalsExactly() {
        assertEquals(new BigDecimal("1.000001"), ProviderValues.evmQuantity("0xf4241", 6));
        assertEquals(new BigDecimal("1"), ProviderValues.evmQuantity("0xde0b6b3a7640000", 18));
    }
    @Test public void integerChainUnitsDecodeExactly() {
        assertEquals(new BigDecimal("1.23456789"), ProviderValues.integerUnits("123456789", 8));
        assertEquals(new BigDecimal("0.000000001"), ProviderValues.integerUnits("1", 9));
    }
    @Test(expected=IllegalArgumentException.class) public void negativeBalanceRejected() {
        ProviderValues.integerUnits("-1", 8);
    }
    @Test public void solanaGetBalanceFixtureUsesResultValue() {
        String fixture="{\"jsonrpc\":\"2.0\",\"result\":{\"context\":{\"slot\":99},\"value\":1234567890},\"id\":1}";
        try {
            WalletEntry sol=new WalletEntry(Network.SOLANA,"11111111111111111111111111111111","SOL","test");
            assertEquals(new BigDecimal("1.23456789"), ProviderClient.parseBalance(sol,new JSONObject(fixture)));
        } catch(Exception e) { throw new AssertionError(e); }
    }
    @Test public void badQuoteDoesNotDiscardIndependentGoodQuote() throws Exception {
        WalletEntry eth=new WalletEntry(Network.ETHEREUM,"0x52908400098527886E0F7030069857D2E4169EE7","ETH","ETH");
        WalletEntry btc=new WalletEntry(Network.BITCOIN,"1BoatSLRHtKNngkdXEeobR76b53LETtpyT","BTC","BTC");
        var out=ProviderClient.parsePrices(new JSONObject("{\"ethereum\":{\"usd\":3000,\"cny\":21000,\"last_updated_at\":1700000000},\"bitcoin\":{\"usd\":\"bad\"}}"),List.of(eth,btc));
        assertEquals(new BigDecimal("3000"),out.get(eth.priceKey("USD")).value());
        assertFalse(out.containsKey(btc.priceKey("USD")));
    }
    @Test public void malformedUsdDoesNotDiscardCny() throws Exception {
        WalletEntry eth=new WalletEntry(Network.ETHEREUM,"0x52908400098527886E0F7030069857D2E4169EE7","ETH","ETH");
        var out=ProviderClient.parsePrices(new JSONObject("{\"ethereum\":{\"usd\":\"bad\",\"cny\":21000,\"last_updated_at\":1700000000}}"),List.of(eth));
        assertFalse(out.containsKey(eth.priceKey("USD")));
        assertEquals(new BigDecimal("21000"),out.get(eth.priceKey("CNY")).value());
    }
    @Test public void invalidAndFutureQuotesAreUnavailable() throws Exception {
        WalletEntry eth=new WalletEntry(Network.ETHEREUM,"0x52908400098527886E0F7030069857D2E4169EE7","ETH","ETH");
        for(String fields:List.of("\"usd\":-1,\"cny\":0,\"last_updated_at\":1700000000",
                "\"usd\":3000,\"last_updated_at\":9999999999", "\"usd\":3000")) {
            assertTrue(ProviderClient.parsePrices(new JSONObject("{\"ethereum\":{"+fields+"}}"),List.of(eth)).isEmpty());
        }
    }
    @Test public void bitcoinIgnoresMempoolBalance() throws Exception {
        WalletEntry btc=new WalletEntry(Network.BITCOIN,"1BoatSLRHtKNngkdXEeobR76b53LETtpyT","BTC","test");
        JSONObject response=new JSONObject("{\"chain_stats\":{\"funded_txo_sum\":123456789,\"spent_txo_sum\":100000000},\"mempool_stats\":{\"funded_txo_sum\":900000000,\"spent_txo_sum\":0}}");
        assertEquals(new BigDecimal("0.23456789"),ProviderClient.parseBalance(btc,response));
    }
    @Test public void solanaSumsAllTokenAccountsExactly() throws Exception {
        WalletEntry usdc=new WalletEntry(Network.SOLANA,"11111111111111111111111111111111","USDC","test");
        String account="{\"account\":{\"data\":{\"parsed\":{\"info\":{\"tokenAmount\":{\"amount\":\"%s\",\"decimals\":6}}}}}}";
        JSONObject response=new JSONObject("{\"result\":{\"value\":["+String.format(account,"1000001")+","+String.format(account,"2")+"]}}");
        assertEquals(new BigDecimal("1.000003"),ProviderClient.parseBalance(usdc,response));
    }
}
