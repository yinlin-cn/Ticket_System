package ticketslots;

import java.security.GeneralSecurityException;
import java.util.Arrays;
import java.util.HexFormat;
import javax.crypto.Mac;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;

public final class AuthCrypto {
    private AuthCrypto() {
    }

    public static String challengeProof(char[] password, String saltHex, String nonceHex)
            throws GeneralSecurityException {
        byte[] salt = HexFormat.of().parseHex(saltHex);
        PBEKeySpec spec = new PBEKeySpec(password, salt, 120000, 256);
        byte[] key;
        try {
            key = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
                    .generateSecret(spec)
                    .getEncoded();
        } finally {
            spec.clearPassword();
        }

        try {
            Mac mac = Mac.getInstance("HmacSHA256");
            mac.init(new SecretKeySpec(key, "HmacSHA256"));
            return HexFormat.of().formatHex(
                    mac.doFinal(HexFormat.of().parseHex(nonceHex)));
        } finally {
            Arrays.fill(key, (byte) 0);
        }
    }
}
