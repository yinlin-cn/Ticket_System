package ticketslots;

/**
 * The only interface screens use to reach the long-task runtime.
 */
@FunctionalInterface
public interface FrontendSlot {
    void submit(AppEvent event);
}
