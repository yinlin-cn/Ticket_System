package ticketslots;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import javax.swing.SwingUtilities;

/**
 * Applies semantic UI patches on one component thread. Swing mutations still
 * happen on the EDT; this worker only keeps component operations ordered.
 */
public final class ComponentWorker implements AutoCloseable {
    private final BlockingQueue<Runnable> patches = new LinkedBlockingQueue<>();
    private final Thread worker;
    private volatile boolean running = true;

    public ComponentWorker() {
        worker = new Thread(this::loop, "ticket-component");
        worker.setDaemon(true);
        worker.start();
    }

    public void submit(Runnable patch) {
        if (running) {
            patches.offer(patch);
        }
    }

    private void loop() {
        try {
            while (running) {
                Runnable patch = patches.poll(500, TimeUnit.MILLISECONDS);
                if (patch == null) {
                    continue;
                }
                try {
                    SwingUtilities.invokeAndWait(patch);
                } catch (Exception error) {
                    if (running) {
                        error.printStackTrace(System.err);
                    }
                }
            }
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        }
    }

    @Override
    public void close() {
        running = false;
        worker.interrupt();
    }
}
