package ticketslots.ui;

import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import ticketslots.AppState;
import ticketslots.ComponentWorker;
import ticketslots.UiPort;

public final class SwingUiPort implements UiPort {
    private static final DateTimeFormatter CLOCK =
            DateTimeFormatter.ofPattern("HH:mm:ss");

    private final MainFrame frame;
    private final ComponentWorker components;

    public SwingUiPort(MainFrame frame, ComponentWorker components) {
        this.frame = frame;
        this.components = components;
    }

    @Override
    public void setState(AppState.Snapshot snapshot) {
        components.submit(() -> frame.applyState(snapshot));
    }

    @Override
    public void setBusy(String detail) {
        String safeDetail = detail == null ? "" : detail;
        components.submit(() -> frame.updateBusy(safeDetail));
    }

    @Override
    public void log(String message) {
        String line = LocalTime.now().format(CLOCK) + "  " + message;
        components.submit(() -> frame.appendLog(line));
    }
}
