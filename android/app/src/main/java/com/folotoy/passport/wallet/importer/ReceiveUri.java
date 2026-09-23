package com.folotoy.passport.wallet.importer;

import com.folotoy.passport.wallet.core.AddressValidator;
import com.folotoy.passport.wallet.core.Network;
import java.util.Locale;

public final class ReceiveUri {
    public record Parsed(Network suggestedNetwork,String address){}
    private ReceiveUri(){}
    public static Parsed parse(String payload){
        if(payload==null)throw new IllegalArgumentException("Empty receive code");String lower=payload.toLowerCase(Locale.ROOT);Network network;String value;
        if(lower.startsWith("bitcoin:")){network=Network.BITCOIN;value=payload.substring(8);}
        else if(lower.startsWith("ethereum:")){network=Network.ETHEREUM;value=payload.substring(9);}
        else if(lower.startsWith("solana:")){network=Network.SOLANA;value=payload.substring(7);}
        else return new Parsed(null,payload);
        if(value.contains("?")||value.contains("/")||lower.contains("amount=")||lower.contains("value=")||lower.contains("transfer")||lower.contains("sign"))throw new IllegalArgumentException("This is a payment request, contract call, or signing request. Import only a permanent receiving address.");
        int at=value.indexOf('@');if(at>=0){if(network!=Network.ETHEREUM)throw new IllegalArgumentException("Unexpected network suffix");String chain=value.substring(at+1);if(!chain.equals("1"))network=switch(chain){case "42161"->Network.ARBITRUM;case "56"->Network.BSC;case "8453"->Network.BASE;case "10"->Network.OPTIMISM;case "137"->Network.POLYGON;default->throw new IllegalArgumentException("Unsupported EVM chain id: "+chain);};value=value.substring(0,at);}
        if(!AddressValidator.isValid(network,value))throw new IllegalArgumentException("Invalid receiving address for "+network.label());return new Parsed(network,value);
    }
}
