package ticketslots.tasks;

import java.util.concurrent.atomic.AtomicLong;
import ticketslots.AppEvent;
import ticketslots.ClientTask;
import ticketslots.ProtocolData;
import ticketslots.TaskContext;
import ticketslots.model.TicketType;
import ticketslots.ui.UiText;

public final class ReserveTask extends ClientTask {
    private static final AtomicLong KEY_SEQUENCE = new AtomicLong();
    private boolean requestInFlight;

    @Override
    public String id() {
        return "reserve";
    }

    @Override
    public boolean accepts(AppEvent event) {
        if (AppEvent.RESERVE_REQUEST.equals(event.type())) {
            return true;
        }
        if (AppEvent.NETWORK_DISCONNECTED.equals(event.type())) {
            return true;
        }
        if (AppEvent.REQUEST_TIMEOUT.equals(event.type())) {
            return "reserve".equals(event.get("operation"));
        }
        return AppEvent.SERVER_RESPONSE.equals(event.type())
                && event.get("operation").startsWith("reserve");
    }

    @Override
    public void handle(AppEvent event, TaskContext context) {
        if (AppEvent.RESERVE_REQUEST.equals(event.type())) {
            reserve(event, context);
        } else if (AppEvent.NETWORK_DISCONNECTED.equals(event.type())) {
            requestInFlight = false;
            context.ui().setBusy("");
            context.state().updateTask("purchase", "DISCONNECTED", "", "");
        } else if (AppEvent.REQUEST_TIMEOUT.equals(event.type())) {
            requestInFlight = false;
            context.ui().setBusy("");
            context.state().updateTask("purchase", "TIMEOUT", "", "");
            context.ui().log("[购票] 购票请求超时");
        } else {
            response(event, context);
        }
    }

    private void reserve(AppEvent event, TaskContext context) {
        if (requestInFlight) {
            context.ui().log("[购票] 请等待当前购票结果");
            return;
        }
        if (!context.state().authenticated()) {
            context.ui().log("[购票] 请先登录再提交购票");
            return;
        }
        TicketType ticket = context.state().selectedTicket();
        if (ticket == null) {
            context.ui().log("[购票] 请先选择票种");
            return;
        }
        int quantity = event.getInt("quantity", 1);
        if (quantity < 1 || quantity > 10) {
            context.ui().log("[购票] 购票数量必须在 1 到 10 之间");
            return;
        }
        if (quantity > ticket.remaining()) {
            context.ui().log("[购票] 购票数量超过当前可见余票");
            return;
        }

        String idempotencyKey = "ui-order-" + System.currentTimeMillis()
                + "-" + KEY_SEQUENCE.incrementAndGet();
        String payload = "order.reserve|" + context.state().token() + "|"
                + ticket.id() + "|" + quantity + "|" + idempotencyKey;
        context.ui().setBusy("正在提交购票请求");
        context.state().updateTask("purchase", "QUEUING", ticket.id(), "");
        requestInFlight = true;
        context.protocol().request("reserve", payload, "reserve", "reserve.result");
        context.ui().log("[购票] 已提交 " + ticket.id()
                + " 的购票请求，数量 " + quantity + " 张");
    }

    private void response(AppEvent event, TaskContext context) {
        String operation = event.get("operation");
        String[] parts = ProtocolData.header(event);

        if ("reserve.queued".equals(operation)) {
            String orderNo = ProtocolData.part(parts, 2);
            context.state().lastOrderNo(orderNo);
            context.state().updateTask("purchase", "QUEUED", orderNo, "");
            context.ui().log("[购票] 订单 " + orderNo
                    + " 已进入队列，正在等待数据库处理");
            return;
        }

        if ("reserve.error".equals(operation)) {
            requestInFlight = false;
            context.ui().setBusy("");
            context.state().updateTask("purchase", "REJECTED", "",
                    UiText.errorDetail(parts));
            context.ui().log("[购票] 购票请求被拒绝："
                    + UiText.errorDetail(parts));
            return;
        }

        if ("reserve.result".equals(operation)) {
            requestInFlight = false;
            context.ui().setBusy("");
            if (event.getBoolean("ok", false)) {
                String orderNo = ProtocolData.part(parts, 2);
                String amount = ProtocolData.part(parts, 3);
                String status = ProtocolData.part(parts, 4);
                context.state().lastOrderNo(orderNo);
                context.state().updateTask("purchase", status, orderNo,
                        "金额 " + money(amount));
                context.ui().log("[购票] 订单 " + orderNo + " 已创建，金额 "
                        + money(amount) + "，状态 "
                        + UiText.orderStatus(status));
                context.eventBus().submit(AppEvent.of(AppEvent.CATALOG_REQUEST));
                context.eventBus().submit(AppEvent.of(AppEvent.ORDERS_REQUEST));
            } else {
                String code = ProtocolData.part(parts, 3);
                String reason = ProtocolData.part(parts, 4);
                context.state().updateTask("purchase", "REJECTED " + code, "",
                        UiText.error(code, reason));
                context.ui().log("[购票] 下单失败："
                        + UiText.error(code, reason));
            }
        }
    }

    private static String money(String cents) {
        try {
            long value = Long.parseLong(cents);
            return String.format("%d.%02d", value / 100, Math.abs(value % 100));
        } catch (NumberFormatException error) {
            return cents;
        }
    }
}
