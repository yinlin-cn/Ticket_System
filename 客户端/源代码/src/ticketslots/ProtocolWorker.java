package ticketslots;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Serializes pending-request bookkeeping and response decoding. The server
 * protocol has no request id on the wire, so this thread is the single owner
 * of the local request-to-operation mapping.
 */
public final class ProtocolWorker implements AutoCloseable {
    private static final long REQUEST_TIMEOUT_SECONDS = 60;

    private final NetworkClient network;
    private final FrontendSlot eventBus;
    private final ScheduledExecutorService worker =
            Executors.newSingleThreadScheduledExecutor(runnable -> {
                Thread thread = new Thread(runnable, "ticket-protocol");
                thread.setDaemon(true);
                return thread;
            });
    private final AtomicLong requestSequence = new AtomicLong();
    private final Map<String, Deque<PendingRequest>> pendingByCommand = new HashMap<>();

    public ProtocolWorker(NetworkClient network, FrontendSlot eventBus) {
        this.network = network;
        this.eventBus = eventBus;
    }

    public String request(String operation, String payload, String expectedCommand,
                          String followUpCommand) {
        String requestId = "req-" + requestSequence.incrementAndGet();
        worker.execute(() -> register(requestId, operation, payload,
                expectedCommand, followUpCommand));
        return requestId;
    }

    public void onWire(AppEvent wireEvent) {
        String payload = wireEvent.get("payload");
        worker.execute(() -> decode(payload));
    }

    public void resetPending(String reason) {
        worker.execute(() -> {
            for (Deque<PendingRequest> queue : pendingByCommand.values()) {
                for (PendingRequest pending : queue) {
                    pending.active = false;
                    if (pending.timeout != null) {
                        pending.timeout.cancel(false);
                    }
                    submitTimeout(pending, reason);
                }
            }
            pendingByCommand.clear();
        });
    }

    private void register(String requestId, String operation, String payload,
                          String expectedCommand, String followUpCommand) {
        PendingRequest pending = new PendingRequest(
                requestId, operation, payload, expectedCommand,
                followUpCommand == null ? "" : followUpCommand);
        pending.timeout = worker.schedule(
                () -> expire(pending),
                REQUEST_TIMEOUT_SECONDS,
                TimeUnit.SECONDS);
        enqueue(expectedCommand, pending);

        if (!network.send(payload)) {
            remove(pending);
            pending.active = false;
            pending.timeout.cancel(false);
            deliver(pending, expectedCommand,
                    expectedCommand + "|error|CLIENT_SEND_FAILED|network is not connected",
                    false, true, pending.operation + ".error");
        }
    }

    private void decode(String frame) {
        if (frame == null || frame.isBlank()) {
            return;
        }
        String header = firstLine(frame);
        String[] parts = header.split("\\|", -1);
        String command = parts[0];

        PendingRequest pending = take(command);
        if (pending == null) {
            pending = takeByFollowUp(command);
        }
        if (pending == null) {
            eventBus.submit(AppEvent.of(AppEvent.SERVER_RESPONSE, "command", command)
                    .with("operation", "push")
                    .with("payload", frame)
                    .with("requestId", "")
                    .with("ok", isPositive(parts))
                    .with("final", true));
            return;
        }

        boolean positive = isPositive(parts);
        boolean queued = parts.length > 1 && "queued".equals(parts[1]);
        boolean finalResponse = true;
        String eventOperation = pending.operation;

        if (!pending.followUpCommand.isEmpty()
                && pending.followUpCommand.equals(command)) {
            eventOperation = pending.operation + ".result";
            finish(pending);
        } else if (queued && !pending.followUpCommand.isEmpty()) {
            finalResponse = false;
            eventOperation = pending.operation + ".queued";
            enqueue(pending.followUpCommand, pending);
        } else if (isInitialFailure(command, parts)) {
            eventOperation = pending.operation + ".error";
            finish(pending);
        } else {
            finish(pending);
        }

        deliver(pending, command, frame, positive, finalResponse, eventOperation);
    }

    private void deliver(PendingRequest pending, String command, String payload,
                         boolean positive, boolean finalResponse) {
        deliver(pending, command, payload, positive, finalResponse, pending.operation);
    }

    private void deliver(PendingRequest pending, String command, String payload,
                         boolean positive, boolean finalResponse, String operation) {
        eventBus.submit(AppEvent.of(AppEvent.SERVER_RESPONSE, "operation", operation)
                .with("command", command)
                .with("payload", payload)
                .with("requestId", pending.requestId)
                .with("ok", positive)
                .with("final", finalResponse));
    }

    private void expire(PendingRequest pending) {
        if (!pending.active) {
            return;
        }
        pending.active = false;
        remove(pending);
        submitTimeout(pending, "request timed out");
    }

    private void submitTimeout(PendingRequest pending, String reason) {
        eventBus.submit(AppEvent.of(AppEvent.REQUEST_TIMEOUT, "requestId", pending.requestId)
                .with("operation", pending.operation)
                .with("reason", reason));
    }

    private void finish(PendingRequest pending) {
        pending.active = false;
        if (pending.timeout != null) {
            pending.timeout.cancel(false);
        }
    }

    private void enqueue(String command, PendingRequest pending) {
        pendingByCommand.computeIfAbsent(command, ignored -> new ArrayDeque<>())
                .addLast(pending);
    }

    private PendingRequest take(String command) {
        Deque<PendingRequest> queue = pendingByCommand.get(command);
        if (queue == null || queue.isEmpty()) {
            return null;
        }
        PendingRequest pending = queue.removeFirst();
        if (queue.isEmpty()) {
            pendingByCommand.remove(command);
        }
        return pending;
    }

    private PendingRequest takeByFollowUp(String command) {
        for (Map.Entry<String, Deque<PendingRequest>> entry : pendingByCommand.entrySet()) {
            Iterator<PendingRequest> iterator = entry.getValue().iterator();
            while (iterator.hasNext()) {
                PendingRequest pending = iterator.next();
                if (command.equals(pending.followUpCommand)) {
                    iterator.remove();
                    if (entry.getValue().isEmpty()) {
                        pendingByCommand.remove(entry.getKey());
                    }
                    return pending;
                }
            }
        }
        return null;
    }

    private void remove(PendingRequest pending) {
        for (Map.Entry<String, Deque<PendingRequest>> entry : pendingByCommand.entrySet()) {
            entry.getValue().removeIf(candidate -> candidate == pending);
        }
        pendingByCommand.entrySet().removeIf(entry -> entry.getValue().isEmpty());
    }

    private static boolean isPositive(String[] parts) {
        return parts.length > 1
                && ("ok".equals(parts[1]) || "queued".equals(parts[1]));
    }

    private static boolean isInitialFailure(String command, String[] parts) {
        return parts.length > 1
                && "error".equals(parts[1])
                && ("reserve".equals(command)
                    || "refund".equals(command)
                    || "refund.submit".equals(command));
    }

    private static String firstLine(String frame) {
        int newline = frame.indexOf('\n');
        return newline < 0 ? frame : frame.substring(0, newline);
    }

    @Override
    public void close() {
        worker.execute(() -> {
            for (Deque<PendingRequest> queue : pendingByCommand.values()) {
                for (PendingRequest pending : queue) {
                    pending.active = false;
                    if (pending.timeout != null) {
                        pending.timeout.cancel(false);
                    }
                }
            }
            pendingByCommand.clear();
        });
        worker.shutdown();
    }

    private static final class PendingRequest {
        private final String requestId;
        private final String operation;
        private final String payload;
        private final String expectedCommand;
        private final String followUpCommand;
        private ScheduledFuture<?> timeout;
        private boolean active = true;

        private PendingRequest(String requestId, String operation, String payload,
                               String expectedCommand, String followUpCommand) {
            this.requestId = requestId;
            this.operation = operation;
            this.payload = payload;
            this.expectedCommand = expectedCommand;
            this.followUpCommand = followUpCommand;
        }
    }
}
