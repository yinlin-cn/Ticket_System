package ticketslots;

import java.util.Objects;
import java.util.Set;

/**
 * The single write path for top-level navigation state.
 *
 * <p>Business tasks decide when and where to navigate. This service only
 * validates the destination and records the accepted navigation.</p>
 */
public final class NavigationService {
    private static final Set<String> SCREENS = Set.of(
            "connection", "login", "catalog", "buy", "orders", "refund", "log");

    private final AppState state;

    public NavigationService(AppState state) {
        this.state = Objects.requireNonNull(state, "state");
    }

    public synchronized boolean go(String screenId, String source) {
        if (screenId == null || !SCREENS.contains(screenId)) {
            return false;
        }
        state.showScreen(screenId);
        state.updateTask("navigation", "NAVIGATED", screenId,
                source == null ? "" : source);
        return true;
    }
}
