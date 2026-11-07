package ticketslots.tasks;

import java.util.concurrent.atomic.AtomicLong;
import ticketslots.AppEvent;
import ticketslots.ClientTask;
import ticketslots.ProtocolData;
import ticketslots.TaskContext;
import ticketslots.model.OrderRecord;
import ticketslots.ui.UiText;

public final class RefundTask extends ClientTask {
    private static final AtomicLong KEY_SEQUENCE = new AtomicLong();
    private boolean requestInFlight;

    @Override
    public String id() {
        return "refund";
    }

    @Override
    public boolean accepts(AppEvent event) {
        if (AppEvent.REFUND_REQUEST.equals(event.type())) {
            return true;
        }
        if (AppEvent.NETWORK_DISCONNECTED.equals(event.type())) {
            return true;
        }
        if (AppEvent.REQUEST_TIMEOUT.equals(event.type())) {
            return "refund".equals(event.get("operation"));
        }
        return AppEvent.SERVER_RESPONSE.equals(event.type())
                && event.get("operation").startsWith("refund");
    }

    @Override
    public void handle(AppEvent event, TaskContext context) {
        if (AppEvent.REFUND_REQUEST.equals(event.type())) {
            refund(event, context);
        } else if (AppEvent.NETWORK_DISCONNECTED.equals(event.type())) {
            requestInFlight = false;
            context.ui().setBusy("");
            context.state().updateTask("refund", "DISCONNECTED", "", "");
        } else if (AppEvent.REQUEST_TIMEOUT.equals(event.type())) {
            requestInFlight = false;
            context.ui().setBusy("");
            context.state().updateTask("refund", "TIMEOUT", "", "");
            context.ui().log("[退票] 退票请求超时");
        } else {
            response(event, context);
        }
    }

    private void refund(AppEvent event, TaskContext context) {
        if (requestInFlight) {
            context.ui().log("[退票] 请等待当前退票结果");
            return;
        }
        if (!context.state().authenticated()) {
            context.ui().log("[退票] 请先登录再申请退票");
            return;
        }
        OrderRecord order = context.state().selectedOrder();
        if (order == null) {
            context.ui().log("[退票] 请先选择订单");
            return;
        }
        int available = order.quantity() - order.refundedQuantity();
        int quantity = event.getInt("quantity", 1);
        if (quantity < 1 || quantity > 10 || quantity > available) {
            context.ui().log("[退票] 退票数量必须在 1 到 " + available + " 之间");
            return;
        }

        String key = "ui-refund-" + System.currentTimeMillis()
                + "-" + KEY_SEQUENCE.incrementAndGet();
        String payload = "refund.submit|" + context.state().token() + "|"
                + order.orderNo() + "|" + quantity + "|" + key + "|"
                + order.ticketType();
        context.ui().setBusy("正在提交退票申请");
        context.state().updateTask("refund", "QUEUING",
                order.orderNo(), "数量 " + quantity + " 张");
        requestInFlight = true;
        context.protocol().request("refund", payload, "refund", "refund.result");
        context.ui().log("[退票] 已提交订单 " + order.orderNo()
                + " 的退票申请，数量 " + quantity + " 张");
    }

    private void response(AppEvent event, TaskContext context) {
        String operation = event.get("operation");
        String[] parts = ProtocolData.header(event);

        if ("refund.queued".equals(operation)) {
            String refundNo = ProtocolData.part(parts, 2);
            context.state().updateTask("refund", "QUEUED", refundNo, "");
            context.ui().log("[退票] 退票单 " + refundNo
                    + " 已进入队列，正在等待数据库处理");
            return;
        }
        if ("refund.error".equals(operation)) {
            requestInFlight = false;
            context.ui().setBusy("");
            context.state().updateTask("refund", "REJECTED", "",
                    UiText.errorDetail(parts));
            context.ui().log("[退票] 退票申请被拒绝："
                    + UiText.errorDetail(parts));
            return;
        }
        if (!"refund.result".equals(operation)) {
            return;
        }

        context.ui().setBusy("");
        requestInFlight = false;
        if (event.getBoolean("ok", false)) {
            String refundNo = ProtocolData.part(parts, 2);
            String orderNo = ProtocolData.part(parts, 3);
            String quantity = ProtocolData.part(parts, 4);
            String status = ProtocolData.part(parts, 5);
            String remaining = ProtocolData.part(parts, 6);
            context.state().updateTask("refund", status, orderNo,
                    "可售余量 " + remaining);
            context.ui().log("[退票] 订单 " + orderNo + " 的退票单 "
                    + refundNo + " 已完成，退票 " + quantity + " 张，状态 "
                    + UiText.orderStatus(status));
            context.eventBus().submit(AppEvent.of(AppEvent.CATALOG_REQUEST));
            context.eventBus().submit(AppEvent.of(AppEvent.ORDERS_REQUEST));
        } else {
            String orderNo = ProtocolData.part(parts, 3);
            String code = ProtocolData.part(parts, 4);
            String detail = ProtocolData.part(parts, 5);
            context.state().updateTask("refund", "REJECTED " + code,
                    orderNo, UiText.error(code, detail));
            context.ui().log("[退票] 退票失败："
                    + UiText.error(code, detail));
        }
    }
}
