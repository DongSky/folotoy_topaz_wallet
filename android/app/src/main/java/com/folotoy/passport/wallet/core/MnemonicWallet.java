package com.folotoy.passport.wallet.core;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.text.Normalizer;
import java.util.Arrays;
import java.util.List;
import org.bitcoinj.core.Base58;
import org.bitcoinj.core.SegwitAddress;
import org.bitcoinj.crypto.ChildNumber;
import org.bitcoinj.crypto.DeterministicKey;
import org.bitcoinj.crypto.HDKeyDerivation;
import org.bitcoinj.crypto.MnemonicCode;
import org.bitcoinj.crypto.MnemonicException;
import org.bitcoinj.params.MainNetParams;
import org.bouncycastle.crypto.digests.KeccakDigest;
import org.bouncycastle.crypto.digests.SHA512Digest;
import org.bouncycastle.crypto.generators.PKCS5S2ParametersGenerator;
import org.bouncycastle.crypto.macs.HMac;
import org.bouncycastle.crypto.params.Ed25519PrivateKeyParameters;
import org.bouncycastle.crypto.params.KeyParameter;

/** Offline, English BIP39 public-address derivation. No seed or private key leaves this class.
 * Callers own and must clear their input char arrays. Scratch byte arrays are cleared best effort;
 * Java Strings, library BigIntegers and garbage-collected library objects cannot be reliably erased.
 */
public final class MnemonicWallet {
    public record Accounts(String bitcoin, String ethereum, String solana,
                           String btcPath, String evmPath, String solPath) {
        public String address(Network network) {
            if (network == Network.BITCOIN) return bitcoin;
            if (network == Network.SOLANA) return solana;
            if (network != null && network.evm()) return ethereum;
            throw new IllegalArgumentException("Unsupported network");
        }
    }
    private MnemonicWallet() {}

    public static Accounts derive(char[] phrase, char[] passphrase, int accountIndex) {
        if (phrase == null || passphrase == null) throw new IllegalArgumentException("请输入助记词");
        if (accountIndex < 0) throw new IllegalArgumentException("账户序号必须为非负整数");
        String normalized = Normalizer.normalize(new String(phrase), Normalizer.Form.NFKD)
                .trim().replaceAll("\\s+", " ");
        List<String> words = Arrays.asList(normalized.split(" "));
        if (!List.of(12, 15, 18, 21, 24).contains(words.size()))
            throw new IllegalArgumentException("助记词应为 12、15、18、21 或 24 个英文单词");
        byte[] entropy = null;
        byte[] password = null;
        byte[] salt = null;
        byte[] seed = null;
        try {
            // bitcoinj bundles and checks the SHA-256 digest of its BIP39 English wordlist.
            entropy = new MnemonicCode().toEntropy(words);
            password = normalized.getBytes(StandardCharsets.UTF_8);
            salt = ("mnemonic" + Normalizer.normalize(new String(passphrase), Normalizer.Form.NFKD))
                    .getBytes(StandardCharsets.UTF_8);
            PKCS5S2ParametersGenerator pbkdf = new PKCS5S2ParametersGenerator(new SHA512Digest());
            pbkdf.init(password, salt, 2048);
            seed = ((KeyParameter) pbkdf.generateDerivedParameters(512)).getKey();
            String btcPath = "m/84'/0'/" + accountIndex + "'/0/0";
            String evmPath = "m/44'/60'/0'/0/" + accountIndex;
            String solPath = "m/44'/501'/" + accountIndex + "'/0'";
            DeterministicKey master = HDKeyDerivation.createMasterPrivateKey(seed);
            DeterministicKey bitcoin = descend(master, hardened(84), hardened(0), hardened(accountIndex), 0, 0);
            DeterministicKey ethereum = descend(master, hardened(44), hardened(60), hardened(0), 0, accountIndex);
            String btc = SegwitAddress.fromKey(MainNetParams.get(), bitcoin).toString();
            String eth = ethereumAddress(ethereum);
            String sol = solanaAddress(seed, accountIndex);
            return new Accounts(btc, eth, sol, btcPath, evmPath, solPath);
        } catch (MnemonicException e) {
            // Do not propagate library exceptions: a word exception contains secret input.
            throw new IllegalArgumentException("助记词单词或校验和不正确，请检查顺序和拼写");
        } catch (java.io.IOException e) {
            throw new IllegalStateException("助记词词库不可用");
        } finally {
            wipe(entropy); wipe(password); wipe(salt); wipe(seed);
        }
    }

    private static int hardened(int index) { return index | ChildNumber.HARDENED_BIT; }
    private static DeterministicKey descend(DeterministicKey key, int... path) {
        for (int child : path) key = HDKeyDerivation.deriveChildKey(key, new ChildNumber(child));
        return key;
    }
    private static String ethereumAddress(DeterministicKey key) {
        byte[] publicKey = key.getPubKeyPoint().getEncoded(false);
        KeccakDigest digest = new KeccakDigest(256);
        digest.update(publicKey, 1, 64);
        byte[] hash = new byte[32];
        digest.doFinal(hash, 0);
        StringBuilder out = new StringBuilder("0x");
        for (int i = 12; i < hash.length; i++) {
            out.append(Character.forDigit((hash[i] >>> 4) & 15, 16));
            out.append(Character.forDigit(hash[i] & 15, 16));
        }
        return out.toString();
    }
    private static String solanaAddress(byte[] seed, int index) {
        // SLIP-0010 Ed25519 accepts only hardened child derivation.
        byte[] node = hmac("ed25519 seed".getBytes(StandardCharsets.US_ASCII), seed);
        byte[] data = new byte[37];
        byte[] chain = null;
        byte[] privateSeed = null;
        try {
            for (int child : new int[]{44, 501, index, 0}) {
                System.arraycopy(node, 0, data, 1, 32);
                ByteBuffer.wrap(data, 33, 4).putInt(hardened(child));
                chain = Arrays.copyOfRange(node, 32, 64);
                byte[] next = hmac(chain, data);
                wipe(node); wipe(chain);
                node = next;
            }
            privateSeed = Arrays.copyOf(node, 32);
            byte[] publicKey = new Ed25519PrivateKeyParameters(privateSeed, 0).generatePublicKey().getEncoded();
            return Base58.encode(publicKey);
        } finally {
            wipe(node); wipe(data); wipe(chain); wipe(privateSeed);
        }
    }
    private static byte[] hmac(byte[] key, byte[] data) {
        HMac mac = new HMac(new SHA512Digest());
        mac.init(new KeyParameter(key));
        mac.update(data, 0, data.length);
        byte[] out = new byte[64];
        mac.doFinal(out, 0);
        return out;
    }
    private static void wipe(byte[] bytes) { if (bytes != null) Arrays.fill(bytes, (byte) 0); }
}
