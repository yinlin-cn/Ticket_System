package ticketslots;

/**
 * The long-task side sees only semantic UI operations, not Swing classes.
 */
public interface UiPort {
    void setState(AppState.Snapshot snapshot);

    void setBusy(String detail);

    void log(String message);
}
