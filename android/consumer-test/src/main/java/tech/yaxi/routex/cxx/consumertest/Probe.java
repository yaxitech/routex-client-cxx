package tech.yaxi.routex.cxx.consumertest;

/** Runs one request through the published client and reports how it ended. */
public final class Probe {

    static {
        System.loadLibrary("consumer");
    }

    private Probe() {}

    /**
     * Searches with an invalid ticket and returns the outcome: the name of the error the client
     * produced, {@code RequestError: <detail>} when the request never reached routex, or
     * {@code exception: <detail>} when something crossed the library boundary as an exception.
     */
    public static native String search();
}
