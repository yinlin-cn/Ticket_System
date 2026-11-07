package ticketslots.tasks;

import java.util.ArrayList;
import java.util.List;
import ticketslots.AppEvent;
import ticketslots.ClientTask;
import ticketslots.ProtocolData;
import ticketslots.TaskContext;
import ticketslots.model.OrderRecord;
import ticketslots.ui.UiText;

public final class OrderTask extends ClientTask {
    @Override
    public String id() {
        return "orders";
    }

    @Override
    public boolean accepts(AppEvent event) {
        if (AppEvent.ORDERS_REQUEST.equals(event.type())
                || AppEvent.ORDER_STATUS_REQUEST.equals(event.type())
                || AppEvent.ORDER_SELECTED.equals(event.type())) {
            return true;
        }
        if (!AppEvent.SERVER_RESPONSE.equals(event.type())) {
            return false;
        }
        String operation = event.get("operation");
        return "orders.list".equals(operation) || "order.status".equals(operation);
    }

    @Override
    public void handle(AppEvent event, TaskContext context) {
        switch (event.type()) {
            case AppEvent.ORDERS_REQUEST -> requestOrders(context);
            case AppEvent.ORDER_STATUS_REQUEST -> requestStatus(event, context);
            case AppEvent.ORDER_SELECTED -> selectOrder(event, context);
            case AppEvent.SERVER_RESPONSE -> response(event, context);
            default -> {
            }
        }
    }

    private void requestOrders(TaskContext context) {
        if (!context.state().authenticated()) {
            context.ui().log("[订单] 请先登录再加载订单");
            return;
        }
        context.ui().setBusy("正在加载订单");
        context.state().updateTask("orders", "LOADING", "", "");
        context.protocol().request("orders.list",
                "order.my|" + context.state().token(), "order.my", "");
    }

    private void requestStatus(AppEvent event, TaskContext context) {
        if (!context.state().authenticated()) {
            context.ui().log("[订单] 请先登录再查询订单状态");
            return;
        }
        String orderNo = event.get("order");
        if (orderNo.isEmpty()) {
            OrderRecord selected = context.state().selectedOrder();
            orderNo = selected == null ? context.state().lastOrderNo() : selected.orderNo();
        }
        if (orderNo.isEmpty()) {
            context.ui().log("[订单] 当前没有选中的订单");
            return;
        }
        context.ui().setBusy("正在查询订单 " + orderNo);
        context.state().updateTask("orders", "LOOKING_UP", orderNo, "");
        context.protocol().request("order.status",
                "order.status|" + context.state().token() + "|" + orderNo,
                "order.status", "");
    }

    private void selectOrder(AppEvent event, TaskContext context) {
        if (!(event.value("order") instanceof OrderRecord order)) {
            context.ui().log("[订单] 所选订单数据不完整");
            return;
        }
        context.state().selectedOrder(order);
        context.state().updateTask("orders", order.status(), order.orderNo(), "");
        context.state().updateTask("refund", "READY", order.orderNo(), "");
        context.navigation().go("refund", "orders.select");
        context.ui().log("[订单] 已选择订单 " + order.orderNo());
    }

    private void response(AppEvent event, TaskContext context) {
        context.ui().setBusy("");
        String[] parts = ProtocolData.header(event);
        if (!event.getBoolean("ok", false)) {
            String operation = event.get("operation");
            context.ui().log("[订单] "
                    + UiText.operationName(operation) + "失败："
                    + UiText.errorDetail(parts));
            context.state().updateTask("orders", "ERROR", "", 
                    UiText.errorDetail(parts));
            return;
        }

        if ("orders.list".equals(event.get("operation"))) {
            List<OrderRecord> orders = new ArrayList<>();
            for (String line : ProtocolData.lines(event)) {
                String[] row = line.split("\\|", -1);
                if (row.length < 7 || "order.my".equals(row[0])) {
                    continue;
                }
                try {
                    orders.add(new OrderRecord(
                            row[0], row[1], Integer.parseInt(row[2]),
                            Long.parseLong(row[3]), row[4],
                            Integer.parseInt(row[5]), row[6]));
                } catch (NumberFormatException ignored) {
                    // Skip a malformed row rather than corrupt the whole table.
                }
            }
            context.state().orders(orders);
            context.state().updateTask("orders", "LOADED", "",
                    UiText.orders(orders.size()));
            context.ui().log("[订单] 已加载 " + UiText.orders(orders.size()));
            return;
        }

        if ("order.status".equals(event.get("operation")) && parts.length >= 7) {
            try {
                OrderRecord order = new OrderRecord(
                        parts[2], parts[3], Integer.parseInt(parts[4]),
                        Long.parseLong(parts[5]), parts[6],
                        parts.length > 7 ? Integer.parseInt(parts[7]) : 0, "");
                context.state().orderStatus(order);
                context.state().updateTask("orders", order.status(),
                        order.orderNo(), "");
                context.state().updateTask("refund", "READY",
                        order.orderNo(), "");
                context.ui().log("[订单] " + order.orderNo()
                        + "，状态 " + UiText.orderStatus(order.status())
                        + "，已退 " + order.refundedQuantity() + " 张");
            } catch (NumberFormatException error) {
                context.ui().log("[订单] 订单状态响应格式无效");
            }
        }
    }
}
