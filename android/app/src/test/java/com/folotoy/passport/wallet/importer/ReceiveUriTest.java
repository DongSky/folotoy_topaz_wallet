package com.folotoy.passport.wallet.importer;

import static org.junit.Assert.*;
import com.folotoy.passport.wallet.core.Network;
import org.junit.Test;

public class ReceiveUriTest {
    @Test public void extractsCommonPermanentReceiveUris() {
        assertEquals(new ReceiveUri.Parsed(Network.BITCOIN,"1BoatSLRHtKNngkdXEeobR76b53LETtpyT"),ReceiveUri.parse("bitcoin:1BoatSLRHtKNngkdXEeobR76b53LETtpyT"));
        assertEquals(new ReceiveUri.Parsed(Network.ETHEREUM,"0x52908400098527886E0F7030069857D2E4169EE7"),ReceiveUri.parse("ethereum:0x52908400098527886E0F7030069857D2E4169EE7@1"));
        assertEquals(new ReceiveUri.Parsed(Network.SOLANA,"11111111111111111111111111111111"),ReceiveUri.parse("solana:11111111111111111111111111111111"));
    }
    @Test(expected=IllegalArgumentException.class) public void rejectsAmountBearingPaymentRequest() { ReceiveUri.parse("bitcoin:1BoatSLRHtKNngkdXEeobR76b53LETtpyT?amount=1"); }
    @Test(expected=IllegalArgumentException.class) public void rejectsEthereumContractCall() { ReceiveUri.parse("ethereum:0x52908400098527886E0F7030069857D2E4169EE7/transfer?uint256=1"); }
}
