package com.folotoy.passport.wallet.core;

import java.math.BigInteger;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Locale;

public final class AddressValidator {
    private static final String B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    private AddressValidator() {}

    public static boolean isValid(Network network, String address) {
        if (address == null || address.length() > 90 || !address.equals(address.trim())) return false;
        if (network.evm()) return address.matches("0x[0-9a-fA-F]{40}");
        if (network == Network.SOLANA) return decodeBase58(address, 32) != null;
        if (network == Network.BITCOIN) {
            String lower = address.toLowerCase(Locale.ROOT);
            if (lower.startsWith("bc1")) return validSegwit(address);
            byte[] raw = decodeBase58(address, -1);
            if (raw == null || raw.length != 25 || (raw[0] != 0 && raw[0] != 5)) return false;
            try {
                byte[] check = MessageDigest.getInstance("SHA-256").digest(
                        MessageDigest.getInstance("SHA-256").digest(Arrays.copyOf(raw, 21)));
                return Arrays.equals(Arrays.copyOfRange(raw, 21, 25), Arrays.copyOf(check, 4));
            } catch (Exception e) { return false; }
        }
        return false;
    }

    private static boolean validSegwit(String address) {
        if (address.length() < 14 || address.length() > 90 || !(address.equals(address.toLowerCase(Locale.ROOT)) || address.equals(address.toUpperCase(Locale.ROOT)))) return false;
        String a=address.toLowerCase(Locale.ROOT);int split=a.lastIndexOf('1');if(split!=2||!a.substring(0,split).equals("bc")||a.length()-split-1<7)return false;
        String alphabet="qpzry9x8gf2tvdw0s3jn54khce6mua7l";int[] data=new int[a.length()-split-1];for(int i=0;i<data.length;i++){data[i]=alphabet.indexOf(a.charAt(split+1+i));if(data[i]<0)return false;}
        int polymod=1;for(char c:a.substring(0,split).toCharArray())polymod=polymodStep(polymod,c>>5);polymod=polymodStep(polymod,0);for(char c:a.substring(0,split).toCharArray())polymod=polymodStep(polymod,c&31);for(int v:data)polymod=polymodStep(polymod,v);
        int version=data[0];if(version>16||((version==0&&polymod!=1)||(version>0&&polymod!=0x2bc830a3)))return false;
        int acc=0,bits=0,count=0;for(int i=1;i<data.length-6;i++){int v=data[i];acc=(acc<<5)|v;bits+=5;while(bits>=8){bits-=8;count++;}}
        if(bits>=5||((acc<<(8-bits))&255)!=0)return false;return version==0?(count==20||count==32):(count>=2&&count<=40);
    }
    private static int polymodStep(int pre,int value){int b=pre>>>25,chk=(pre&0x1ffffff)<<5^value;int[] g={0x3b6a57b2,0x26508e6d,0x1ea119fa,0x3d4233dd,0x2a1462b3};for(int i=0;i<5;i++)if(((b>>>i)&1)!=0)chk^=g[i];return chk;}

    private static byte[] decodeBase58(String value, int exactLength) {
        if (value == null || value.isEmpty()) return null;
        BigInteger n = BigInteger.ZERO;
        for (int i = 0; i < value.length(); i++) {
            int d = B58.indexOf(value.charAt(i)); if (d < 0) return null;
            n = n.multiply(BigInteger.valueOf(58)).add(BigInteger.valueOf(d));
        }
        byte[] b = n.equals(BigInteger.ZERO) ? new byte[0] : n.toByteArray();
        if (b.length > 0 && b[0] == 0) b = Arrays.copyOfRange(b, 1, b.length);
        int zeros = 0; while (zeros < value.length() && value.charAt(zeros) == '1') zeros++;
        byte[] out = new byte[zeros + b.length]; System.arraycopy(b, 0, out, zeros, b.length);
        return exactLength >= 0 && out.length != exactLength ? null : out;
    }
}
