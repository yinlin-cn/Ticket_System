package ticketslots.tasks;

import ticketslots.AppEvent;
import ticketslots.ClientTask;
import ticketslots.TaskContext;
import ticketslots.ui.UiText;

public final class NavigationTask extends ClientTask {
    @Override
    public String id() {
        return "navigation";
    }

    @Override
    public boolean accepts(AppEvent event) {
        return AppEvent.NAVIGATE_REQUEST.equals(event.type());
    }

    @Override
    public void handle(AppEvent event, TaskContext context) {
        String screen = event.get("screen");
        String source = event.get("source", "event");
        if (screen.isBlank() || !context.navigation().go(screen, source)) {
            context.state().updateTask("navigation", "REJECTED", screen,
                    "目标页面不存在");
            context.ui().log("[导航] 目标页面不存在：" + UiText.screenName(screen));
            return;
        }
        context.ui().log("[导航] " + UiText.sourceName(source)
                + " -> " + UiText.screenName(screen));
    }
}
