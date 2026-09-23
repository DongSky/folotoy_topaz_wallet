package com.folotoy.passport.wallet.provider;

import com.folotoy.passport.wallet.core.AssetRegistry;
import com.folotoy.passport.wallet.core.Network;
import com.folotoy.passport.wallet.core.WalletEntry;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.math.BigDecimal;
import java.net.HttpURLConnection;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/** Fixed HTTPS endpoints only. Call exclusively from the user-triggered refresh action. */
public final class ProviderClient {
    private static final Map<Network,String> RPC=Map.of(
        Network.ETHEREUM,"https://ethereum-rpc.publicnode.com", Network.ARBITRUM,"https://arb1.arbitrum.io/rpc",
        Network.SOLANA,"https://api.mainnet-beta.solana.com", Network.BSC,"https://bsc-dataseed.binance.org",
        Network.BASE,"https://mainnet.base.org", Network.OPTIMISM,"https://mainnet.optimism.io",
        Network.POLYGON,"https://polygon-bor-rpc.publicnode.com");
    private static final String BTC="https://mempool.space/api/address/";
    private static final String PRICES="https://api.coingecko.com/api/v3/simple/price";
    private ProviderClient(){}

    public static BigDecimal balance(WalletEntry entry) throws Exception {
        AssetRegistry.Asset asset = AssetRegistry.get(entry.network(), entry.asset());
        if (entry.network() == Network.BITCOIN) {
            return parseBalance(entry, new JSONObject(get(BTC + entry.address())));
        }
        String method;
        JSONArray params;
        if (entry.network() == Network.SOLANA) {
            if (asset.contractOrMint() == null) {
                method = "getBalance";
                params = new JSONArray().put(entry.address())
                    .put(new JSONObject().put("commitment", "confirmed"));
            } else {
                method = "getTokenAccountsByOwner";
                params = new JSONArray().put(entry.address())
                    .put(new JSONObject().put("mint", asset.contractOrMint()))
                    .put(new JSONObject().put("encoding", "jsonParsed").put("commitment", "confirmed"));
            }
        } else if (asset.contractOrMint() == null) {
            method = "eth_getBalance";
            params = new JSONArray().put(entry.address()).put("latest");
        } else {
            method = "eth_call";
            String padded = "0".repeat(24) + entry.address().substring(2).toLowerCase(Locale.ROOT);
            params = new JSONArray().put(new JSONObject().put("to", asset.contractOrMint())
                .put("data", "0x70a08231" + padded)).put("latest");
        }
        return parseBalance(entry, rpc(RPC.get(entry.network()), method, params));
    }

    /** Production response parser, also exercised with provider-shaped fixtures. */
    static BigDecimal parseBalance(WalletEntry entry, JSONObject response) throws Exception {
        AssetRegistry.Asset asset = AssetRegistry.get(entry.network(), entry.asset());
        if (response.has("error")) throw new java.io.IOException("Provider RPC error");
        if (entry.network() == Network.BITCOIN) {
            JSONObject confirmed = response.getJSONObject("chain_stats");
            BigDecimal funded = ProviderValues.integerUnits(confirmed.get("funded_txo_sum").toString(), 8);
            BigDecimal spent = ProviderValues.integerUnits(confirmed.get("spent_txo_sum").toString(), 8);
            BigDecimal balance = funded.subtract(spent).stripTrailingZeros();
            if (balance.signum() < 0) throw new java.io.IOException("Negative confirmed balance");
            return balance;
        }
        if (entry.network() == Network.SOLANA) {
            JSONObject result = response.getJSONObject("result");
            if (asset.contractOrMint() == null) {
                return ProviderValues.integerUnits(result.get("value").toString(), asset.decimals());
            }
            BigDecimal sum = BigDecimal.ZERO;
            JSONArray accounts = result.getJSONArray("value");
            for (int i = 0; i < accounts.length(); i++) {
                JSONObject info = accounts.getJSONObject(i).getJSONObject("account").getJSONObject("data")
                    .getJSONObject("parsed").getJSONObject("info");
                if (info.has("mint") && !asset.contractOrMint().equals(info.getString("mint"))) {
                    throw new java.io.IOException("Provider returned an unexpected token mint");
                }
                JSONObject amount = info.getJSONObject("tokenAmount");
                if (amount.getInt("decimals") != asset.decimals()) {
                    throw new java.io.IOException("Provider token decimals disagree with registry");
                }
                sum = sum.add(ProviderValues.integerUnits(amount.getString("amount"), asset.decimals()));
            }
            return sum.stripTrailingZeros();
        }
        return ProviderValues.evmQuantity(response.getString("result"), asset.decimals());
    }

    public record PriceQuote(BigDecimal value, java.time.Instant providerTimestamp) {}

    public static Map<String,PriceQuote> prices(Iterable<WalletEntry> entries) throws Exception {
        java.util.LinkedHashSet<String> ids = new java.util.LinkedHashSet<>();
        for (WalletEntry entry : entries) ids.add(AssetRegistry.get(entry.network(), entry.asset()).priceId());
        if (ids.isEmpty()) return Map.of();
        JSONObject response = new JSONObject(get(PRICES + "?ids=" + String.join(",", ids)
            + "&vs_currencies=usd,cny&include_last_updated_at=true"));
        return parsePrices(response, entries);
    }

    /** Missing or invalid keys remain absent so callers retain and mark only those caches failed. */
    static Map<String,PriceQuote> parsePrices(JSONObject response, Iterable<WalletEntry> entries) {
        Map<String,PriceQuote> quotes = new LinkedHashMap<>();
        long latestAllowed = java.time.Instant.now().getEpochSecond() + 300;
        for (WalletEntry entry : entries) {
            JSONObject row = response.optJSONObject(AssetRegistry.get(entry.network(), entry.asset()).priceId());
            if (row == null) continue;
            long timestamp = row.optLong("last_updated_at", 0);
            if (timestamp <= 0 || timestamp > latestAllowed) continue;
            for (String currency : java.util.List.of("USD", "CNY")) {
                try {
                    String raw = row.get(currency.toLowerCase(Locale.ROOT)).toString();
                    if (raw.length() > 64) continue;
                    BigDecimal value = new BigDecimal(raw);
                    if (value.signum() <= 0 || Math.abs((long)value.scale()) > 30 || value.precision() > 30) continue;
                    quotes.put(entry.priceKey(currency), new PriceQuote(value, java.time.Instant.ofEpochSecond(timestamp)));
                } catch (Exception ignored) {
                    // The other currency and other assets can still succeed.
                }
            }
        }
        return quotes;
    }
    private static JSONObject rpc(String endpoint,String method,JSONArray params)throws Exception{JSONObject r=new JSONObject(post(endpoint,new JSONObject().put("jsonrpc","2.0").put("id",1).put("method",method).put("params",params).toString()));if(r.has("error"))throw new java.io.IOException("Provider RPC error: "+r.getJSONObject("error").optString("message","unknown"));if(!r.has("result"))throw new java.io.IOException("Provider response has no result");return r;}
    private static String get(String u)throws Exception{return request(u,null);}
    private static String post(String u,String body)throws Exception{return request(u,body);}
    private static String request(String u,String body)throws Exception{
        URI uri=URI.create(u);for(int redirect=0;redirect<2;redirect++){if(!"https".equals(uri.getScheme()))throw new IllegalArgumentException("HTTPS required");HttpURLConnection c=(HttpURLConnection)uri.toURL().openConnection();try{c.setInstanceFollowRedirects(false);c.setConnectTimeout(8000);c.setReadTimeout(10000);c.setRequestProperty("Accept","application/json");if(body!=null){c.setRequestMethod("POST");c.setDoOutput(true);c.setRequestProperty("Content-Type","application/json");try(OutputStream o=c.getOutputStream()){o.write(body.getBytes(StandardCharsets.UTF_8));}}int code=c.getResponseCode();if(code>=300&&code<400){String location=c.getHeaderField("Location");if(location==null)throw new java.io.IOException("Provider redirect missing location");uri=uri.resolve(location);continue;}InputStream in=code>=200&&code<300?c.getInputStream():c.getErrorStream();if(in==null)throw new java.io.IOException("Provider HTTP "+code);ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] chunk=new byte[8192];int total=0;try(in){for(int n;(n=in.read(chunk))>=0;){total+=n;if(total>2_000_000)throw new java.io.IOException("Provider response exceeds 2 MB");bytes.write(chunk,0,n);}}if(code<200||code>=300)throw new java.io.IOException("Provider HTTP "+code);return new String(bytes.toByteArray(),StandardCharsets.UTF_8);}finally{c.disconnect();}}throw new java.io.IOException("Too many provider redirects");
    }
}
