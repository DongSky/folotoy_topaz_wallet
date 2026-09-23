package com.folotoy.passport.wallet.core;

import org.junit.Test;
import static org.junit.Assert.*;
import java.util.Arrays;
import org.bitcoinj.crypto.MnemonicCode;

public class MnemonicWalletTest {
    // Public BIP39/BIP84 test vector, never a funded wallet.
    private static final String WORDS = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    private MnemonicWallet.Accounts derive(String pass, int index) {
        return MnemonicWallet.derive(WORDS.toCharArray(), pass.toCharArray(), index);
    }
    @Test public void standardBip84EthereumAndSolanaAddresses() {
        var a = derive("", 0);
        assertEquals("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu", a.bitcoin());
        assertEquals("0x9858effd232b4033e47d90003d41ec34ecaeda94", a.ethereum());
        assertEquals("HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk", a.solana());
        assertEquals("m/84'/0'/0'/0/0", a.btcPath());
        assertEquals("m/44'/60'/0'/0/0", a.evmPath());
        assertEquals("m/44'/501'/0'/0'", a.solPath());
        for (Network network : Network.values()) if (network.evm()) assertEquals(a.ethereum(), a.address(network));
        for (Network network : Network.values()) assertTrue(AddressValidator.isValid(network, a.address(network)));
    }
    @Test public void differentIndexProducesDifferentPublicAccounts() {
        var zero = derive("", 0);
        var one = derive("", 1);
        assertEquals("0x6fac4d18c912343bf86fa7049364dd4e424ab9c0", one.ethereum());
        assertNotEquals(zero.bitcoin(), one.bitcoin());
        assertNotEquals(zero.solana(), one.solana());
        assertEquals("m/84'/0'/1'/0/0", one.btcPath());
        assertEquals("m/44'/501'/1'/0'", one.solPath());
    }
    @Test public void bip39PassphraseChangesAccountsAndNormalizesNfkd() {
        assertEquals("0x9c32f71d4db8fb9e1a58b0a80df79935e7256fa6", derive("TREZOR", 0).ethereum());
        assertNotEquals(derive("", 0), derive("TREZOR", 0));
        assertEquals(derive("caf\u00e9", 0), derive("cafe\u0301", 0));
        assertNotEquals(derive("pass", 0), derive(" pass ", 0));
    }
    @Test public void acceptsEveryBip39LengthAndWhitespace() throws Exception {
        MnemonicCode code = new MnemonicCode();
        for (int bytes : new int[]{16,20,24,28,32}) {
            String words = String.join(" ", code.toMnemonic(new byte[bytes]));
            assertNotNull(MnemonicWallet.derive(words.toCharArray(), new char[0], 0).bitcoin());
        }
        assertEquals(derive("", 0), MnemonicWallet.derive(("  " + WORDS.replace(" ", "\n\t") + " ").toCharArray(), new char[0], 0));
    }
    @Test public void rejectsInvalidChecksumWordCountAndNegativeIndexWithoutLeakingWords() {
        for (String invalid : new String[]{WORDS.replace("about", "abandon"), WORDS.replace("about", "secretword"), "abandon ".repeat(9).trim()}) {
            IllegalArgumentException error = assertThrows(IllegalArgumentException.class, () -> MnemonicWallet.derive(invalid.toCharArray(), new char[0], 0));
            assertFalse(error.getMessage().contains("secretword"));
            assertNull(error.getCause());
        }
        assertThrows(IllegalArgumentException.class, () -> derive("", -1));
    }
    @Test public void callerOwnsInputBuffers() {
        char[] phrase = WORDS.toCharArray(), pass = "TREZOR".toCharArray();
        MnemonicWallet.derive(phrase, pass, 0);
        assertArrayEquals(WORDS.toCharArray(), phrase);
        assertArrayEquals("TREZOR".toCharArray(), pass);
        Arrays.fill(phrase, '\0'); Arrays.fill(pass, '\0');
    }
}
