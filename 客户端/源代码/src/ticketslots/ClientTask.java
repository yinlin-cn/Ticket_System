package ticketslots;

/**
 * Base class for task modules inserted into the long-task event slots.
 * A task owns one business state area and never owns a network thread.
 */
public abstract class ClientTask {
    public abstract String id();

    public abstract boolean accepts(AppEvent event);

    public abstract void handle(AppEvent event, TaskContext context) throws Exception;

    public void start(TaskContext context) {
    }

    public void stop() {
    }
}
