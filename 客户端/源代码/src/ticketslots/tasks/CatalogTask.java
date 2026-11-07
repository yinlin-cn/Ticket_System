package ticketslots.tasks;

import java.util.ArrayList;
import java.util.List;
import ticketslots.AppEvent;
import ticketslots.ClientTask;
import ticketslots.ProtocolData;
import ticketslots.TaskContext;
import ticketslots.model.TicketType;
import ticketslots.ui.UiText;

public final class CatalogTask extends ClientTask {
    @Override
    public String id() {
        return "catalog";
    }

    @Override
    public boolean accepts(AppEvent event) {
        if (AppEvent.CATALOG_REQUEST.equals(event.type())
                || AppEvent.TICKET_SELECTED.equals(event.type())) {
            return true;
        }
        return AppEvent.SERVER_RESPONSE.equals(event.type())
                && "catalog.list".equals(event.get("operation"));
    }

    @Override
    public void handle(AppEvent event, TaskContext context) {
        if (AppEvent.CATALOG_REQUEST.equals(event.type())) {
            requestCatalog(context);
        } else if (AppEvent.TICKET_SELECTED.equals(event.type())) {
            selectTicket(event, context);
        } else {
            handleCatalog(event, context);
        }
    }

    private void requestCatalog(TaskContext context) {
        if (!context.state().connected()) {
            context.ui().log("[票务] 请先连接服务器再加载票务目录");
            return;
        }
        context.ui().setBusy("正在加载票务目录");
        context.state().updateTask("catalog", "LOADING", "", "");
        context.protocol().request("catalog.list", "catalog.list|", "catalog", "");
    }

    private void selectTicket(AppEvent event, TaskContext context) {
        if (!(event.value("ticket") instanceof TicketType ticket)) {
            context.ui().log("[票务] 所选票种数据不完整");
            return;
        }
        context.state().selectedTicket(ticket);
        context.state().updateTask("purchase", "READY", ticket.id(), "");
        context.navigation().go("buy", "catalog.select");
        context.ui().log("[票务] 已选择 " + ticket.eventName()
                + " / " + ticket.ticketName());
    }

    private void handleCatalog(AppEvent event, TaskContext context) {
        context.ui().setBusy("");
        String[] lines = ProtocolData.lines(event);
        String[] header = lines[0].split("\\|", -1);
        if (!ProtocolData.ok(header)) {
            context.state().updateTask("catalog", "ERROR", "",
                    UiText.errorDetail(header));
            context.ui().log("[票务] 加载失败："
                    + UiText.errorDetail(header));
            return;
        }

        List<TicketType> tickets = new ArrayList<>();
        for (int i = 1; i < lines.length; i++) {
            String[] row = lines[i].split("\\|", -1);
            if (row.length < 5) {
                continue;
            }
            long price;
            int remaining;
            try {
                price = Long.parseLong(row[3]);
                remaining = Integer.parseInt(row[4]);
            } catch (NumberFormatException error) {
                continue;
            }
            tickets.add(new TicketType(row[0], row[1], row[2], price, remaining));
        }
        context.state().catalog(tickets);
        context.state().updateTask("catalog", "LOADED", "",
                UiText.ticketTypes(tickets.size()));
        context.ui().log("[票务] 已加载 " + UiText.ticketTypes(tickets.size()));
    }
}
