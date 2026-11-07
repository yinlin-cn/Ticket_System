package ticketslots;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * One persistent framed connection with a blocking reader and writer.
 * Neither thread touches Swing; all transitions are posted to the event bus.
 */
public final class NetworkClient implements AutoCloseable {
    private static final int MAX_FRAME_BYTES = 9999;
    private static final int OUTBOUND_CAPACITY = 1024;
    private static final long HEARTBEAT_NANOS = TimeUnit.SECONDS.toNanos(30);

    private final FrontendSlot eventBus;
    private final BlockingQueue<String> outbound =
            new ArrayBlockingQueue<>(OUTBOUND_CAPACITY);
    private final AtomicBoolean open = new AtomicBoolean();
    private final AtomicBoolean stopRequested = new AtomicBoolean();
    private volatile Socket socket;
    private volatile Thread readerThread;
    private volatile Thread writerThread;

    public NetworkClient(FrontendSlot eventBus) {
        this.eventBus = eventBus;
    }

    public synchronized void connect(String host, int port) throws IOException {
        disconnect("reconnect");
        stopRequested.set(false);
        outbound.clear();

        Socket next = new Socket();
        next.connect(new InetSocketAddress(host, port), 5000);
        next.setTcpNoDelay(true);
        next.setKeepAlive(true);
        socket = next;
        open.set(true);

        readerThread = new Thread(this::readLoop, "ticket-network-reader");
        writerThread = new Thread(this::writeLoop, "ticket-network-writer");
        readerThread.setDaemon(true);
        writerThread.setDaemon(true);
        readerThread.start();
        writerThread.start();

        eventBus.submit(AppEvent.of(AppEvent.NETWORK_CONNECTED)
                .with("host", host)
                .with("port", port));
    }

    public boolean isConnected() {
        return open.get();
    }

    public boolean send(String payload) {
        if (!open.get()) {
            return false;
        }
        if (payload == null || payload.getBytes(StandardCharsets.UTF_8).length > MAX_FRAME_BYTES) {
            eventBus.submit(AppEvent.of(AppEvent.NETWORK_ERROR, "detail",
                    "outgoing frame is empty or exceeds 9999 bytes"));
            return false;
        }
        return outbound.offer(payload);
    }

    public synchronized void disconnect(String reason) {
        boolean wasOpen = open.getAndSet(false);
        stopRequested.set(true);
        Socket current = socket;
        socket = null;
        if (current != null) {
            try {
                current.close();
            } catch (IOException ignored) {
                // A broken socket is already disconnected.
            }
        }
        if (wasOpen) {
            eventBus.submit(AppEvent.of(AppEvent.NETWORK_DISCONNECTED, "reason", reason));
        }
    }

    private void readLoop() {
        String reason = "connection closed";
        try {
            InputStream in = socket.getInputStream();
            while (open.get()) {
                String payload = readFrame(in);
                eventBus.submit(AppEvent.of(AppEvent.WIRE, "payload", payload));
            }
        } catch (EOFException error) {
            reason = "server closed the connection";
        } catch (IOException error) {
            if (open.get()) {
                reason = error.getMessage() == null ? "read failed" : error.getMessage();
            }
        } finally {
            boolean wasOpen = open.getAndSet(false);
            if (wasOpen) {
                closeQuietly();
                eventBus.submit(AppEvent.of(AppEvent.NETWORK_DISCONNECTED, "reason", reason));
            }
        }
    }

    private void writeLoop() {
        try {
            OutputStream out = socket.getOutputStream();
            long lastWrite = System.nanoTime();
            while (open.get()) {
                String payload = outbound.poll(500, TimeUnit.MILLISECONDS);
                if (payload == null) {
                    long now = System.nanoTime();
                    if (now - lastWrite >= HEARTBEAT_NANOS) {
                        writeFrame(out, "ping|");
                        lastWrite = now;
                    }
                    continue;
                }
                writeFrame(out, payload);
                lastWrite = System.nanoTime();
            }
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        } catch (IOException error) {
            if (open.get()) {
                eventBus.submit(AppEvent.of(AppEvent.NETWORK_ERROR, "detail",
                        "write failed: " + error.getMessage()));
                disconnect("write failed");
            }
        }
    }

    private void closeQuietly() {
        Socket current = socket;
        socket = null;
        if (current != null) {
            try {
                current.close();
            } catch (IOException ignored) {
                // Ignore close errors after a failed connection.
            }
        }
    }

    private static void writeFrame(OutputStream out, String payload) throws IOException {
        byte[] body = payload.getBytes(StandardCharsets.UTF_8);
        if (body.length == 0 || body.length > MAX_FRAME_BYTES) {
            throw new IOException("invalid frame length " + body.length);
        }
        byte[] header = String.format("%04d", body.length)
                .getBytes(StandardCharsets.US_ASCII);
        out.write(header);
        out.write(body);
        out.flush();
    }

    private static String readFrame(InputStream in) throws IOException {
        byte[] header = readFully(in, 4, true);
        if (header == null) {
            throw new EOFException("connection closed");
        }
        final int length;
        try {
            length = Integer.parseInt(new String(header, StandardCharsets.US_ASCII));
        } catch (NumberFormatException error) {
            throw new IOException("bad frame header");
        }
        if (length <= 0 || length > MAX_FRAME_BYTES) {
            throw new IOException("bad frame length " + length);
        }
        byte[] body = readFully(in, length, false);
        if (body == null) {
            throw new EOFException("connection closed inside a frame");
        }
        return new String(body, StandardCharsets.UTF_8);
    }

    private static byte[] readFully(InputStream in, int size, boolean allowCleanEof)
            throws IOException {
        byte[] buffer = new byte[size];
        int offset = 0;
        while (offset < size) {
            int count = in.read(buffer, offset, size - offset);
            if (count < 0) {
                if (allowCleanEof && offset == 0) {
                    return null;
                }
                throw new EOFException("connection closed inside a frame");
            }
            offset += count;
        }
        return buffer;
    }

    @Override
    public synchronized void close() {
        stopRequested.set(true);
        boolean wasOpen = open.getAndSet(false);
        closeQuietly();
        Thread reader = readerThread;
        Thread writer = writerThread;
        if (reader != null) {
            reader.interrupt();
        }
        if (writer != null) {
            writer.interrupt();
        }
        readerThread = null;
        writerThread = null;
        if (wasOpen) {
            eventBus.submit(AppEvent.of(AppEvent.NETWORK_DISCONNECTED, "reason",
                    "client closed"));
        }
    }
}
