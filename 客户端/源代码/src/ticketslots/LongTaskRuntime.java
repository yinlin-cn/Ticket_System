package ticketslots;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * The long-task thread owns application state and dispatches events to the
 * task modules that accept them. Screens only submit events through the
 * FrontendSlot interface.
 */
public final class LongTaskRuntime implements FrontendSlot, AutoCloseable {
    private static final int EVENT_CAPACITY = 2048;

    private final BlockingQueue<AppEvent> events = new ArrayBlockingQueue<>(EVENT_CAPACITY);
    private final List<ClientTask> tasks = new ArrayList<>();
    private final UiPort ui;
    private final Thread worker;
    private volatile boolean running = true;
    private ProtocolWorker protocol;
    private TaskContext context;

    public LongTaskRuntime(UiPort ui) {
        this.ui = ui;
        this.worker = new Thread(this::loop, "ticket-long-task");
        this.worker.setDaemon(true);
    }

    public void configure(TaskContext context, ProtocolWorker protocol) {
        this.context = context;
        this.protocol = protocol;
    }

    public void addTask(ClientTask task) {
        tasks.add(task);
    }

    public void start() {
        for (ClientTask task : tasks) {
            task.start(context);
        }
        publishState();
        worker.start();
    }

    @Override
    public void submit(AppEvent event) {
        if (!events.offer(event)) {
            ui.log("[队列] 前端事件队列已满，已丢弃事件 " + event.type());
        }
    }

    private void loop() {
        try {
            while (running) {
                AppEvent event = events.poll(500, TimeUnit.MILLISECONDS);
                if (event == null) {
                    continue;
                }
                if (AppEvent.WIRE.equals(event.type())) {
                    protocol.onWire(event);
                    continue;
                }
                boolean handled = false;
                for (ClientTask task : tasks) {
                    if (!task.accepts(event)) {
                        continue;
                    }
                    handled = true;
                    try {
                        task.handle(event, context);
                    } catch (Exception error) {
                        String message = error.getClass().getSimpleName() + ": "
                                + error.getMessage();
                        context.state().updateTask(task.id(), "ERROR", "", message);
                        ui.log("[任务:" + task.id() + "] 执行失败："
                                + message);
                    }
                }
                if (handled) {
                    publishState();
                }
            }
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        }
    }

    private void publishState() {
        ui.setState(context.state().snapshot());
    }

    @Override
    public void close() {
        running = false;
        worker.interrupt();
        for (ClientTask task : tasks) {
            task.stop();
        }
    }
}
