package tech.yaxi.routex.cxx.consumertest;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Demonstrates the missing rustls initialization.
 *
 * <p>The client links reqwest with rustls, whose platform verifier has to be handed a JVM and a
 * Context before it can check a certificate. Nothing in this package does that, so the first HTTPS
 * request fails in the transport rather than at routex. This test is the acceptance criterion for
 * fixing that: it fails today, and the failure message carries the error the client produced.
 */
@RunWith(AndroidJUnit4.class)
public class RustlsInitializationTest {

    @Test
    public void reachesRoutexOverTls() {
        String outcome = Probe.search();
        assertNotNull(outcome);

        // An invalid ticket is expected to be rejected — by routex. Getting that far means the
        // handshake succeeded; a transport error or an exception means it did not.
        assertFalse(
                "expected routex to answer, but the request never got there: " + outcome,
                outcome.startsWith("RequestError") || outcome.startsWith("exception"));
    }
}
